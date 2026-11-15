/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#include "instruction_effects.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include <cafe/classpath/class_hierarchy.hpp>

#include "detail/descriptor.hpp"
#include "detail/member_signature.hpp"
#include "io/class_format.hpp"
#include "io/code_operands.hpp"

namespace cafe::analysis {
namespace {

/**
 * @brief Short implementation alias for verification value categories.
 */
using Kind = VerificationKind;
/**
 * @brief Successful verification step or a structured AnalysisError.
 */
using Status = std::expected<void, AnalysisError>;
/**
 * @brief Verification value or a structured AnalysisError.
 */
using ValueResult = std::expected<VerificationType, AnalysisError>;

constexpr VerificationType INT{Kind::Integer}; ///< Canonical int verification category.
constexpr VerificationType FLOAT{Kind::Float}; ///< Canonical float verification category.
constexpr VerificationType LONG{Kind::Long}; ///< Canonical long verification category occupying two slots.
constexpr VerificationType DOUBLE{Kind::Double}; ///< Canonical double verification category occupying two slots.
constexpr VerificationType NULL_VALUE{Kind::Null}; ///< Canonical null-reference verification value.
constexpr VerificationType REFERENCE{Kind::Object}; ///< Generic reference expectation for category-only stack checks.
constexpr VerificationType UNINITIALIZED_THIS{Kind::UninitializedThis}; ///< Constructor receiver verification value before initialization.
constexpr VerificationType THROWABLE{Kind::Object, "java/lang/Throwable"}; ///< java/lang/Throwable verification identity for catch-all handlers and athrow checks.
constexpr std::string_view OBJECT = "java/lang/Object"; ///< JVM java/lang/Object identity used for reference-type rules.

/**
 * @brief Construct the operation's structured failure while owning diagnostic text.
 * @param kind Payload or error discriminator.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected result carrying the appropriate error classification and location.
 * @ingroup implementation
 */
std::unexpected<AnalysisError> fail(AnalysisErrorKind kind, std::string detail) {
  return std::unexpected(AnalysisError{.kind = kind, .detail = std::move(detail)});
}

/**
 * @brief Wrap a class-resolution failure as an instruction analysis error.
 * @param error Underlying structured failure to translate or enrich.
 * @return An unexpected AnalysisError retaining the resolution cause.
 * @ingroup implementation
 */
std::unexpected<AnalysisError> resolution_failure(classpath::ResolutionError error) {
  std::string message = error.detail;
  if (!error.name.empty())
    message = error.name + ": " + message;
  return std::unexpected(AnalysisError{.kind = AnalysisErrorKind::TypeResolutionFailure,
                                       .detail = std::move(message),
                                       .resolution = std::move(error)});
}

/**
 * @brief Recognize uninitialized receiver/allocation verification values.
 * @param value Verification type to test for an uninitialized receiver or allocation site.
 * @return True for UninitializedThis or Uninitialized.
 * @ingroup implementation
 */
bool is_uninitialized(VerificationType value) {
  return value.kind == Kind::Uninitialized || value.kind == Kind::UninitializedThis;
}

/**
 * @brief Create an initialized reference verification value borrowing its name.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return An Object verification value.
 * @ingroup implementation
 */
VerificationType object(std::string_view name) {
  return {Kind::Object, name};
}

/**
 * @brief Translate a parsed descriptor type into a verification value.
 * @param type Parsed descriptor type converted into a verification value.
 * @return The corresponding category/reference type; void maps to Top.
 * @ingroup implementation
 */
VerificationType as_type(detail::DescriptorType type) {
  using D = detail::DescriptorKind;
  switch (type.kind) {
    case D::Integer:
      return INT;
    case D::Float:
      return FLOAT;
    case D::Long:
      return LONG;
    case D::Double:
      return DOUBLE;
    case D::Reference:
      return object(type.reference);
    case D::Void:
      return {};
  }
  return {};
}

/**
 * @brief Render a verification value for a diagnostic.
 * @param type Verification type formatted for a diagnostic.
 * @return An owned readable type description.
 * @ingroup implementation
 */
std::string type_name(VerificationType type) {
  switch (type.kind) {
    case Kind::Top:
      return "unusable";
    case Kind::Integer:
      return "int";
    case Kind::Float:
      return "float";
    case Kind::Long:
      return "long";
    case Kind::Double:
      return "double";
    case Kind::Null:
      return "null";
    case Kind::UninitializedThis:
      return "uninitialized this";
    case Kind::Object:
      return type.reference.empty() ? "reference" : std::string(type.reference);
    case Kind::Uninitialized:
      return "uninitialized " + std::string(type.reference);
    case Kind::WideContinuation:
      return "second slot of a category-two local";
  }
  return "unknown value";
}

/**
 * @brief Explain a failed expected-versus-actual verification type check.
 * @param expected Required verification type or selected declaration identity.
 * @param actual Actual incoming verification value.
 * @return An unexpected TypeMismatch analysis error.
 * @ingroup implementation
 */
std::unexpected<AnalysisError> type_mismatch(VerificationType expected, VerificationType actual) {
  return fail(AnalysisErrorKind::TypeMismatch, "expected " + type_name(expected) + ", found " + type_name(actual));
}

/**
 * @brief Remove the top operand value and update slot accounting.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @return The removed value, or StackUnderflow.
 * @ingroup implementation
 */
ValueResult pop(Frame& frame) {
  if (frame.stack.empty())
    return fail(AnalysisErrorKind::StackUnderflow, "operand stack is empty");
  const auto type = frame.stack.back();
  frame.stack.pop_back();
  frame.stack_slots -= type.width();
  return type;
}

/**
 * @brief Append one verification value while maintaining slot accounting.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @param value Verification value to append to the operand stack.
 * @return Success or a stack-size/invalid-value failure.
 * @ingroup implementation
 */
Status push(Frame& frame, VerificationType value) {
  if (frame.stack_slots + value.width() > std::numeric_limits<std::uint16_t>::max())
    return fail(AnalysisErrorKind::StackOverflow, "operand stack exceeds 65535 slots");
  frame.stack.push_back(value);
  frame.stack_slots += value.width();
  return {};
}

/**
 * @brief Empty the operand stack and reset its slot count.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @ingroup implementation
 */
void clear_stack(Frame& frame) {
  frame.stack.clear();
  frame.stack_slots = 0;
}

/**
 * @brief Clear a local and any overlapping category-two head/continuation.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @param index Zero-based index in the corresponding table or slot array.
 * @ingroup implementation
 */
void invalidate_local(Frame& frame, std::size_t index) {
  const auto previous = frame.locals[index];
  if (previous.kind == Kind::WideContinuation && index != 0)
    frame.locals[index - 1] = {};
  if (previous.width() == 2 && index + 1 < frame.locals.size())
    frame.locals[index + 1] = {};
  frame.locals[index] = {};
}

/**
 * @brief Install a local value and its continuation after invalidating overlapping wide locals.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @param index Zero-based index in the corresponding table or slot array.
 * @param type Verification value to store, including any category-two continuation slot.
 * @ingroup implementation
 */
void store_local(Frame& frame, std::size_t index, VerificationType type) {
  invalidate_local(frame, index);
  if (type.width() == 2) {
    invalidate_local(frame, index + 1);
    frame.locals[index + 1] = {Kind::WideContinuation};
  }
  frame.locals[index] = type;
}

constexpr std::array NUMERIC_TYPES{INT, LONG, FLOAT, DOUBLE}; ///< Opcode-family mapping to int, long, float, and double verification values.
constexpr std::array LOCAL_TYPES{INT, LONG, FLOAT, DOUBLE, REFERENCE}; ///< Local load/store family mapping including reference values.

/**
 * @brief Validate an indivisible one- or two-word JVM operand group.
 * @param frame Borrowed frame whose operand-stack grouping is checked.
 * @param end Exclusive upper boundary of the processed interval.
 * @param words Number of JVM stack slots in the indivisible operand group.
 * @return Its value-entry count, or an invalid category/underflow error.
 * @ingroup implementation
 *
 * Validate an indivisible JVM operand group. A two-word group is either one
 * category-two value or exactly two consecutive category-one values.
 */
std::expected<std::size_t, AnalysisError> group_size(const Frame& frame, std::size_t end, unsigned words) {
  if (words == 0)
    return 0;
  if (end == 0)
    return fail(AnalysisErrorKind::StackUnderflow, "stack manipulation needs another operand");
  const auto width = frame.stack[end - 1].width();
  if (width == words)
    return 1;
  if (words == 1)
    return fail(AnalysisErrorKind::InvalidCategoryOperation, "stack manipulation would split a category-two operand");
  if (end < 2)
    return fail(AnalysisErrorKind::StackUnderflow, "stack manipulation needs two category-one operands");
  if (frame.stack[end - 2].width() != 1)
    return fail(AnalysisErrorKind::InvalidCategoryOperation, "stack manipulation would split a category-two operand");
  return 2;
}

/**
 * @brief Apply JVM pop/dup/swap forms with category-aware grouping.
 * @param frame Mutable verifier locals, operand values, and constructor state.
 * @param opcode Opcode whose operand form or metadata is requested.
 * @return Success or an invalid category/underflow error.
 * @ingroup implementation
 */
Status manipulate_stack(Frame& frame, Opcode opcode) {
  struct Shape {
    unsigned top_words;
    unsigned insertion_words;
    bool duplicate;
  };
  // A dup form preserves the top group and inserts it below the insertion
  // group. Each group's independent form check covers all four dup2_x2 forms.
  constexpr std::array SHAPES{
      Shape{1, 0, false}, // pop
      Shape{2, 0, false}, // pop2
      Shape{1, 0, true}, // dup
      Shape{1, 1, true}, // dup_x1
      Shape{1, 2, true}, // dup_x2
      Shape{2, 0, true}, // dup2
      Shape{2, 1, true}, // dup2_x1
      Shape{2, 2, true}, // dup2_x2
      Shape{1, 1, false}, // swap
  };
  const auto shape = SHAPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::Pop)];
  auto top = group_size(frame, frame.stack.size(), shape.top_words);
  if (!top)
    return std::unexpected(std::move(top.error()));
  auto below = group_size(frame, frame.stack.size() - *top, shape.insertion_words);
  if (!below)
    return std::unexpected(std::move(below.error()));
  if (!shape.duplicate && *below == 0) {
    frame.stack.resize(frame.stack.size() - *top);
    frame.stack_slots -= shape.top_words;
    return {};
  }
  if (!shape.duplicate) {
    std::iter_swap(frame.stack.end() - 2, frame.stack.end() - 1);
    return {};
  }
  if (frame.stack_slots + shape.top_words > std::numeric_limits<std::uint16_t>::max())
    return fail(AnalysisErrorKind::StackOverflow, "duplicating operands exceeds 65535 stack slots");

  using Difference = std::pmr::vector<VerificationType>::difference_type;
  const auto top_begin = frame.stack.end() - static_cast<Difference>(*top);
  std::array<VerificationType, 2> duplicated;
  std::copy(top_begin, frame.stack.end(), duplicated.begin());
  std::rotate(top_begin - static_cast<Difference>(*below), top_begin, frame.stack.end());
  frame.stack.insert(frame.stack.end(), duplicated.begin(), duplicated.begin() + static_cast<Difference>(*top));
  frame.stack_slots += shape.top_words;
  return {};
}

/**
 * @brief Combine opcode and constant index for prepared instruction-effect caching.
 * @param opcode Opcode whose operand form or metadata is requested.
 * @param index One-based constant-pool reference; the required tag is determined by this
 *   operation.
 * @return A compact deterministic cache key.
 * @ingroup implementation
 */
constexpr std::uint32_t effect_key(Opcode opcode, CpIndex index) {
  return (static_cast<std::uint32_t>(opcode) << 16) | index.value;
}

/**
 * @brief Recognize field get/put opcodes.
 * @param opcode Opcode whose operand form or metadata is requested.
 * @return True for field operations.
 * @ingroup implementation
 */
constexpr bool field_access(Opcode opcode) {
  return opcode >= Opcode::GetStatic && opcode <= Opcode::PutField;
}

/**
 * @brief Identify field/invocation operations without an instance receiver.
 * @param opcode Opcode whose operand form or metadata is requested.
 * @return True for static access or invokedynamic.
 * @ingroup implementation
 */
constexpr bool static_access(Opcode opcode) {
  return opcode == Opcode::GetStatic || opcode == Opcode::PutStatic ||
         opcode == Opcode::InvokeStatic || opcode == Opcode::InvokeDynamic;
}

} // namespace

InstructionEffects::InstructionEffects(const ClassFile& owner, const Method& method, const classpath::ClassHierarchy* hierarchy,
                                       std::pmr::memory_resource* scratch) :
    owner(owner), method(method), hierarchy(hierarchy), scratch(scratch), array_names(scratch), method_descriptors(scratch),
    constants(scratch) {
}

// class_identity_issue and method_signature_issue validate the class name and descriptor before optional access.
Status InstructionEffects::initialize(Frame& frame, std::uint16_t max_locals) {
  const auto& pool = owner.constant_pool();
  const auto name = pool.get_utf8(method.name_index());
  const auto descriptor = pool.get_utf8(method.descriptor_index());
  if (!name || !descriptor)
    return fail(AnalysisErrorKind::InvalidDescriptor, "method name and descriptor must name valid Utf8 constants");
  if (const auto* issue = detail::method_signature_issue(owner.version(), owner.access_flags(),
                                                         method.access_flags(), *name, *descriptor))
    return fail(AnalysisErrorKind::InvalidDescriptor, issue);
  if (const auto* issue = detail::class_identity_issue(pool, owner.version(), owner.access_flags(),
                                                       owner.this_class(), owner.super_class()))
    return fail(AnalysisErrorKind::InvalidConstantPoolReference, issue);
  self = *detail::class_name(pool, owner.this_class()); // NOLINT(bugprone-unchecked-optional-access)
  if (owner.super_class())
    superclass = *detail::class_name(pool, owner.super_class()); // NOLINT(bugprone-unchecked-optional-access)

  const bool instance = !detail::is_static_method(method.access_flags(), *name);
  const auto receiver_slots = instance ? 1u : 0u;
  if (*detail::method_parameter_slots(*descriptor) + receiver_slots > max_locals) // NOLINT(bugprone-unchecked-optional-access)
    return fail(AnalysisErrorKind::LocalOverflow, "maximum locals cannot hold the method parameters");

  frame.locals.assign(max_locals, {});
  clear_stack(frame);
  frame.constructor_uninitialized = *name == "<init>" && self != "java/lang/Object";
  if (instance)
    frame.locals[0] = frame.constructor_uninitialized ? UNINITIALIZED_THIS : object(self);

  std::size_t slot = receiver_slots;
  const auto signature = detail::parse_method_descriptor(*descriptor, [&](detail::DescriptorType parameter) {
    const auto type = as_type(parameter);
    store_local(frame, slot, type);
    slot += type.width();
  });
  return_type = as_type(signature->return_type); // NOLINT(bugprone-unchecked-optional-access)
  return {};
}

// Instruction factories make opcode-specific operands immutable; each accessor below is selected by its matching opcode.
Status InstructionEffects::prepare(InstructionId at, Instruction instruction, std::uint16_t max_locals) {
  const auto invalid = [at](AnalysisErrorKind kind, std::string_view issue) -> Status {
    return std::unexpected(AnalysisError{.kind = kind, .instruction = at, .detail = std::string(issue)});
  };
  const auto opcode = instruction.opcode();
  const auto flow = opcode_info(opcode).control_flow;
  if (flow == ControlFlow::SubroutineCall || flow == ControlFlow::SubroutineReturn)
    return invalid(AnalysisErrorKind::UnsupportedSubroutineAnalysis, "subroutine instructions need return-address analysis");
  const auto index = instruction.constant_index();
  if (index) {
    const auto found = constants.find(effect_key(opcode, *index));
    if (found != constants.end()) {
      // These operands are encoded in the instruction itself and may
      // differ between uses of the same constant-pool reference.
      const auto limit = found->second.auxiliary_limit;
      if (opcode == Opcode::InvokeInterface && *instruction.interface_count() != limit) // NOLINT(bugprone-unchecked-optional-access)
        return invalid(AnalysisErrorKind::InvalidCode, "invokeinterface count does not match the descriptor slot count");
      if (opcode == Opcode::MultiANewArray && (*instruction.dimensions() == 0 || *instruction.dimensions() > limit)) // NOLINT(bugprone-unchecked-optional-access)
        return invalid(AnalysisErrorKind::InvalidCode, "multianewarray dimensions exceed array rank");
      return {};
    }
  }
  if (const auto error = detail::validate_instruction(instruction, owner.version(), owner.constant_pool(), max_locals))
    return invalid(error->constant_pool ? AnalysisErrorKind::InvalidConstantPoolReference : AnalysisErrorKind::InvalidCode,
                   error->reason);
  if (index) {
    auto effect = read_constant(instruction);
    if (!effect) {
      effect.error().instruction = at;
      return std::unexpected(std::move(effect.error()));
    }
    constants.emplace(effect_key(opcode, *index), *effect);
  }
  return {};
}

std::string_view InstructionEffects::array_of(std::string_view component) {
  std::pmr::string name(scratch);
  name.reserve(component.size() + (component.starts_with('[') ? 1u : 3u));
  name.push_back('[');
  if (component.starts_with('[')) {
    name += component;
  } else {
    name.push_back('L');
    name += component;
    name.push_back(';');
  }
  return *array_names.emplace(std::move(name)).first;
}

// prepare validates the instruction and constant cross-references before this opcode-specific lookup.
std::expected<InstructionEffects::ConstantEffect, AnalysisError> InstructionEffects::read_constant(Instruction instruction) {
  const auto& pool = owner.constant_pool();
  const auto version = owner.version();
  const auto opcode = instruction.opcode();
  const auto* entry = pool.get(*instruction.constant_index()); // NOLINT(bugprone-unchecked-optional-access)
  if (const auto* issue = detail::cp_reference_issue(pool, *entry, version, false))
    return fail(AnalysisErrorKind::InvalidConstantPoolReference, issue);

  ConstantEffect effect;
  if (opcode == Opcode::Ldc || opcode == Opcode::Ldc2W) {
    switch (entry->tag) {
      case CpTag::Integer:
        effect.value = INT;
        break;
      case CpTag::Float:
        effect.value = FLOAT;
        break;
      case CpTag::Long:
        effect.value = LONG;
        break;
      case CpTag::Double:
        effect.value = DOUBLE;
        break;
      case CpTag::String:
        effect.value = object("java/lang/String");
        break;
      case CpTag::Class:
        effect.value = object("java/lang/Class");
        break;
      case CpTag::MethodHandle:
        effect.value = object("java/lang/invoke/MethodHandle");
        break;
      case CpTag::MethodType:
        effect.value = object("java/lang/invoke/MethodType");
        break;
      case CpTag::Dynamic: {
        auto descriptor = *pool.get_utf8(pool.get(entry->second)->second);
        effect.value = as_type(*detail::parse_field_type(descriptor)); // NOLINT(bugprone-unchecked-optional-access)
        break;
      }
      default:
        return fail(AnalysisErrorKind::InvalidConstantPoolReference, "constant has no loadable verification type");
    }
    return effect;
  }

  if (entry->tag == CpTag::Class) {
    const auto name = *pool.get_utf8(entry->first);
    effect.value = object(opcode == Opcode::ANewArray ? array_of(name) : name);
    if (opcode == Opcode::MultiANewArray)
      effect.auxiliary_limit = static_cast<unsigned>(name.find_first_not_of('['));
    return effect;
  }

  const auto* signature = pool.get(entry->second);
  const auto name = *pool.get_utf8(signature->first);
  auto descriptor = *pool.get_utf8(signature->second);
  if (opcode != Opcode::InvokeDynamic) {
    const auto* member_owner = pool.get(entry->first);
    if (const auto* issue = detail::cp_reference_issue(pool, *member_owner, version, false))
      return fail(AnalysisErrorKind::InvalidConstantPoolReference, issue);
    effect.member_class = *pool.get_utf8(member_owner->first);
    effect.member_name = name;
    effect.member_descriptor = descriptor;
    if (effect.member_class.starts_with('[') &&
        (opcode == Opcode::GetField || opcode == Opcode::PutField ||
         opcode == Opcode::InvokeInterface || opcode == Opcode::InvokeSpecial ||
         entry->tag == CpTag::InterfaceMethodref))
      return fail(AnalysisErrorKind::InvalidConstantPoolReference, "member reference cannot name an array for this opcode");
  }
  const bool is_field = field_access(opcode);
  if (effect.member_class == self) {
    const auto declaration = is_field ? owner.find_field(name, descriptor) : owner.find_method(name, descriptor);
    if (declaration) {
      const auto flags = is_field ? owner.fields()[*declaration].access_flags() : owner.methods()[*declaration].access_flags();
      if (has_flag(flags, AccessFlag::Static) != static_access(opcode))
        return fail(AnalysisErrorKind::InvalidCode, "static or instance opcode disagrees with the member declaration");
      effect.declared_here = true;
    }
  }
  if (is_field) {
    effect.value = as_type(*detail::parse_field_type(descriptor)); // NOLINT(bugprone-unchecked-optional-access)
    return effect;
  }

  const auto [cached, added] = method_descriptors.try_emplace(signature->second.value, scratch);
  auto& shape = cached->second;
  if (added) {
    const auto parsed = detail::parse_method_descriptor(descriptor, [&](detail::DescriptorType parameter) {
      shape.arguments.push_back(as_type(parameter));
    });
    if (!parsed) {
      method_descriptors.erase(cached);
      return fail(AnalysisErrorKind::InvalidDescriptor, "invalid invocation descriptor");
    }
    shape.parameter_slots = parsed->parameter_slots;
    shape.result = as_type(parsed->return_type);
  }
  if (shape.parameter_slots == 255 && !static_access(opcode))
    return fail(AnalysisErrorKind::InvalidDescriptor, "invocation descriptor exceeds the parameter slot limit with its receiver");
  effect.arguments = shape.arguments;
  effect.value = shape.result;
  if (opcode == Opcode::InvokeInterface)
    effect.auxiliary_limit = shape.parameter_slots + 1;
  effect.constructor = name == "<init>";
  if (opcode == Opcode::InvokeSpecial && !effect.constructor) {
    if (auto valid = validate_special_owner(entry->tag, effect.member_class); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  return effect;
}

Status InstructionEffects::validate_special_owner(CpTag tag, std::string_view target) {
  if (target == self)
    return {};
  if (tag == CpTag::InterfaceMethodref) {
    if (std::ranges::any_of(owner.interfaces(), [&](CpIndex interface) {
          return detail::class_name(owner.constant_pool(), interface) == target;
        }))
      return {};
    return fail(AnalysisErrorKind::TypeMismatch, "invokespecial interface target must be a direct superinterface");
  }
  if (target == superclass || !hierarchy)
    return {};
  auto assignable = hierarchy->is_assignable(self, target);
  if (!assignable)
    return resolution_failure(std::move(assignable.error()));
  if (*assignable) {
    auto info = hierarchy->resolve(target);
    if (!info)
      return resolution_failure(std::move(info.error()));
    if (!has_flag((*info)->access_flags, AccessFlag::Interface))
      return {};
  }
  return fail(AnalysisErrorKind::TypeMismatch, "invokespecial class target must be the current class or a superclass");
}

Status InstructionEffects::match(VerificationType value, VerificationType expected, bool allow_uninitialized) {
  if (expected.kind != Kind::Object) {
    if (value == expected)
      return {};
    return type_mismatch(expected, value);
  }
  if (value.kind == Kind::Null)
    return {};
  if (is_uninitialized(value)) {
    if (allow_uninitialized)
      return {};
    return fail(AnalysisErrorKind::UninitializedUse, "expected " + type_name(expected) + ", found " + type_name(value));
  }
  if (value.kind != Kind::Object)
    return type_mismatch(expected, value);
  if (!hierarchy || expected.reference.empty() || value.reference == expected.reference)
    return {};
  auto assignable = hierarchy->is_assignable(value.reference, expected.reference);
  if (!assignable)
    return resolution_failure(std::move(assignable.error()));
  if (!*assignable)
    return type_mismatch(expected, value);
  return {};
}

ValueResult InstructionEffects::pop_matching(Frame& frame, VerificationType expected, bool allow_uninitialized) {
  auto value = pop(frame);
  if (!value)
    return value;
  if (auto valid = match(*value, expected, allow_uninitialized); !valid)
    return std::unexpected(std::move(valid.error()));
  return value;
}

Status InstructionEffects::transition(Frame& frame, std::initializer_list<VerificationType> popped,
                                      std::optional<VerificationType> pushed) {
  for (auto type : popped) {
    if (auto value = pop_matching(frame, type); !value)
      return std::unexpected(std::move(value.error()));
  }
  return pushed ? push(frame, *pushed) : Status{};
}

std::expected<bool, AnalysisError> InstructionEffects::requires_protected_receiver(
    const ConstantEffect& effect, bool method_member) {
  const auto owner_name = effect.member_class;
  if (superclass.empty() || owner_name == self || owner_name.starts_with('[') ||
      (superclass == OBJECT && owner_name != superclass))
    return false;

  if (!superclass_chain) {
    // A validated hierarchy closure bounds the superclass walk and rejects
    // cycles. Keep the returned records even when the cache limit is zero.
    auto valid = hierarchy->is_assignable(self, superclass);
    if (!valid)
      return resolution_failure(std::move(valid.error()));
    if (!*valid)
      return fail(AnalysisErrorKind::TypeMismatch, "declared superclass is outside the current hierarchy");
    superclass_chain.emplace(scratch);
    for (std::string_view name = superclass; !name.empty();) {
      auto entry = hierarchy->resolve(name);
      if (!entry)
        return resolution_failure(std::move(entry.error()));
      superclass_chain->push_back(std::move(*entry));
      const auto& parent = superclass_chain->back()->super_name;
      name = parent ? std::string_view(*parent) : std::string_view{};
    }
  }
  const auto package = [](std::string_view binary_name) {
    const auto slash = binary_name.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : binary_name.substr(0, slash);
  };
  auto entry = std::ranges::find(*superclass_chain, owner_name, [](const auto& info) -> std::string_view { return info->name; });
  for (; entry != superclass_chain->end(); ++entry) {
    auto flags = hierarchy->declared_member_flags((*entry)->name, effect.member_name, effect.member_descriptor, method_member);
    if (!flags)
      return resolution_failure(std::move(flags.error()));
    if (*flags)
      return has_flag(**flags, AccessFlag::Protected) && package((*entry)->name) != package(self);
    if (effect.constructor)
      break;
  }
  return false;
}

Status InstructionEffects::check_protected_receiver(VerificationType receiver, ConstantEffect& effect, bool method_member) {
  if (!hierarchy || receiver.kind == Kind::Null || receiver.reference == self)
    return {};
  // Arrays expose Object.clone as a public method.
  if (method_member && effect.member_class == OBJECT && effect.member_name == "clone" &&
      effect.member_descriptor == "()Ljava/lang/Object;" && receiver.reference.starts_with('['))
    return {};
  if (!effect.protected_receiver) {
    auto required = requires_protected_receiver(effect, method_member);
    if (!required)
      return std::unexpected(std::move(required.error()));
    effect.protected_receiver = *required;
  }
  if (*effect.protected_receiver)
    return match(receiver, object(self));
  return {};
}

Status InstructionEffects::execute_field(Frame& frame, Opcode opcode, ConstantEffect& effect) {
  const bool write = opcode == Opcode::PutField || opcode == Opcode::PutStatic;
  if (write) {
    if (auto field_value = pop_matching(frame, effect.value); !field_value)
      return std::unexpected(std::move(field_value.error()));
  }
  if (!static_access(opcode)) {
    auto receiver = pop(frame);
    if (!receiver)
      return std::unexpected(std::move(receiver.error()));
    // Before a this()/super() call, only fields declared in this class may
    // be assigned through the uninitialized receiver.
    const bool early_assignment = opcode == Opcode::PutField && effect.declared_here &&
                                  receiver->kind == Kind::UninitializedThis;
    if (!early_assignment) {
      if (auto valid = match(*receiver, object(effect.member_class)); !valid)
        return valid;
      if (auto valid = check_protected_receiver(*receiver, effect, false); !valid)
        return valid;
    }
  }
  return write ? Status{} : push(frame, effect.value);
}

Status InstructionEffects::initialize_receiver(Frame& frame, ConstantEffect& effect,
                                               std::optional<VerificationType>& initialized) {
  auto receiver = pop(frame);
  if (!receiver)
    return std::unexpected(std::move(receiver.error()));
  const bool this_receiver = receiver->kind == Kind::UninitializedThis;
  if (this_receiver) {
    if (!frame.constructor_uninitialized || (effect.member_class != self && effect.member_class != superclass))
      return fail(AnalysisErrorKind::InvalidConstructorState, "uninitialized this must call a constructor of this class or its direct superclass");
    frame.constructor_uninitialized = false;
  } else if (receiver->kind != Kind::Uninitialized || receiver->reference != effect.member_class) {
    return fail(AnalysisErrorKind::UninitializedUse, "constructor must initialize the class of the particular new allocation site");
  }
  const auto replacement = object(this_receiver ? self : effect.member_class);
  if (auto valid = check_protected_receiver(replacement, effect, true); !valid)
    return valid;
  std::ranges::replace(frame.locals, *receiver, replacement);
  std::ranges::replace(frame.stack, *receiver, replacement);
  initialized = *receiver;
  return {};
}

Status InstructionEffects::execute_invoke(Frame& frame, Opcode opcode, ConstantEffect& effect,
                                          std::optional<VerificationType>& initialized) {
  for (auto argument = effect.arguments.rbegin(); argument != effect.arguments.rend(); ++argument) {
    if (auto value = pop_matching(frame, *argument); !value)
      return std::unexpected(std::move(value.error()));
  }
  if (effect.constructor)
    return initialize_receiver(frame, effect, initialized);
  if (!static_access(opcode)) {
    const auto expected = object(opcode == Opcode::InvokeSpecial ? self : effect.member_class);
    auto receiver = pop_matching(frame, expected);
    if (!receiver)
      return std::unexpected(std::move(receiver.error()));
    if (opcode == Opcode::InvokeVirtual) {
      if (auto valid = check_protected_receiver(*receiver, effect, true); !valid)
        return valid;
    }
  }
  return effect.value.kind == Kind::Top ? Status{} : push(frame, effect.value);
}

Status InstructionEffects::execute_array_access(Frame& frame, Opcode opcode) {
  struct ArrayAccess {
    VerificationType value;
    std::string_view primitive_array;
  };
  constexpr std::array TYPES{
      ArrayAccess{INT, "[I"},
      ArrayAccess{LONG, "[J"},
      ArrayAccess{FLOAT, "[F"},
      ArrayAccess{DOUBLE, "[D"},
      ArrayAccess{REFERENCE, ""},
      ArrayAccess{INT, "[B"},
      ArrayAccess{INT, "[C"},
      ArrayAccess{INT, "[S"},
  };
  const bool store = opcode >= Opcode::IAStore;
  const auto first = store ? Opcode::IAStore : Opcode::IALoad;
  const auto access = TYPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(first)];
  if (store) {
    if (auto value = pop_matching(frame, access.value); !value)
      return std::unexpected(std::move(value.error()));
  }
  if (auto index = pop_matching(frame, INT); !index)
    return std::unexpected(std::move(index.error()));
  auto array = pop_matching(frame, REFERENCE);
  if (!array)
    return std::unexpected(std::move(array.error()));

  auto result = access.value;
  if (array->kind == Kind::Null) {
    if (result.kind == Kind::Object)
      result = NULL_VALUE;
  } else if (hierarchy) {
    if (access.primitive_array.empty()) {
      if (array->reference.starts_with("[L"))
        result = object(array->reference.substr(2, array->reference.size() - 3));
      else if (array->reference.starts_with("[["))
        result = object(array->reference.substr(1));
      else
        return fail(AnalysisErrorKind::TypeMismatch, "expected reference array, found " + type_name(*array));
    } else if (array->reference != access.primitive_array &&
               !(access.primitive_array == "[B" && array->reference == "[Z")) {
      return type_mismatch(object(access.primitive_array), *array);
    }
  }
  // aastore verifies the array and value are reference types. The JVM
  // checks the actual value against the actual component type at run time.
  return store ? Status{} : push(frame, result);
}

Status InstructionEffects::execute_return(Frame& frame, Opcode opcode) {
  if (frame.constructor_uninitialized)
    return fail(AnalysisErrorKind::InvalidConstructorState, "constructor returns before this is initialized");
  if (opcode == Opcode::Return) {
    if (return_type.kind != Kind::Top)
      return fail(AnalysisErrorKind::TypeMismatch, "void return in a method with a result");
  } else {
    const auto opcode_type = LOCAL_TYPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::IReturn)];
    if (opcode_type.kind != return_type.kind)
      return type_mismatch(return_type, opcode_type);
    if (auto result = pop_matching(frame, return_type); !result)
      return std::unexpected(std::move(result.error()));
  }
  // A return consumes the result and discards the rest of the stack.
  clear_stack(frame);
  return {};
}

// prepare caches validated constant and invocation operands before worklist execution.
Status InstructionEffects::execute_constant(Frame& frame, InstructionId at, Instruction instruction,
                                            ConstantEffect& effect, std::optional<VerificationType>& initialized) {
  const auto opcode = instruction.opcode();
  switch (opcode) {
    case Opcode::Ldc:
    case Opcode::Ldc2W:
      return push(frame, effect.value);
    case Opcode::GetStatic:
    case Opcode::PutStatic:
    case Opcode::GetField:
    case Opcode::PutField:
      return execute_field(frame, opcode, effect);
    case Opcode::InvokeVirtual:
    case Opcode::InvokeSpecial:
    case Opcode::InvokeStatic:
    case Opcode::InvokeInterface:
    case Opcode::InvokeDynamic:
      return execute_invoke(frame, opcode, effect, initialized);
    case Opcode::New: {
      const VerificationType allocated{Kind::Uninitialized, effect.value.reference, at};
      if (std::ranges::find(frame.stack, allocated) != frame.stack.end())
        return fail(AnalysisErrorKind::UninitializedUse, "new reuses an allocation site still present on the operand stack");
      std::ranges::replace(frame.locals, allocated, VerificationType{});
      return push(frame, allocated);
    }
    case Opcode::ANewArray:
      return transition(frame, {INT}, effect.value);
    case Opcode::MultiANewArray:
      for (unsigned dimension = 0; dimension < *instruction.dimensions(); ++dimension) { // NOLINT(bugprone-unchecked-optional-access)
        if (auto length = pop_matching(frame, INT); !length)
          return std::unexpected(std::move(length.error()));
      }
      return push(frame, effect.value);
    case Opcode::CheckCast:
      return transition(frame, {REFERENCE}, effect.value);
    case Opcode::InstanceOf:
      return transition(frame, {REFERENCE}, INT);
    default:
      return fail(AnalysisErrorKind::InvalidCode, "unsupported constant operand instruction");
  }
}

// prepare validates opcode-specific operands before execution; immutable instructions retain those operand forms.
Status InstructionEffects::execute(Frame& frame, InstructionId at, Instruction instruction,
                                   std::optional<VerificationType>& initialized) {
  const auto opcode = instruction.opcode();
  if (const auto index = instruction.constant_index()) {
    const auto effect = constants.find(effect_key(opcode, *index));
    if (effect == constants.end())
      return fail(AnalysisErrorKind::InvalidCode, "missing validated constant operand");
    return execute_constant(frame, at, instruction, effect->second, initialized);
  }

  switch (opcode) {
    case Opcode::Nop:
    case Opcode::Goto:
      return {};
    case Opcode::AConstNull:
      return push(frame, NULL_VALUE);
    case Opcode::IConstM1:
    case Opcode::IConst0:
    case Opcode::IConst1:
    case Opcode::IConst2:
    case Opcode::IConst3:
    case Opcode::IConst4:
    case Opcode::IConst5:
    case Opcode::Bipush:
    case Opcode::Sipush:
      return push(frame, INT);
    case Opcode::LConst0:
    case Opcode::LConst1:
      return push(frame, LONG);
    case Opcode::FConst0:
    case Opcode::FConst1:
    case Opcode::FConst2:
      return push(frame, FLOAT);
    case Opcode::DConst0:
    case Opcode::DConst1:
      return push(frame, DOUBLE);
    case Opcode::ILoad:
    case Opcode::LLoad:
    case Opcode::FLoad:
    case Opcode::DLoad:
    case Opcode::ALoad: {
      const auto type = LOCAL_TYPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::ILoad)];
      const auto index = *instruction.local_index(); // NOLINT(bugprone-unchecked-optional-access)
      const auto value = frame.locals[index];
      if (auto valid = match(value, type, opcode == Opcode::ALoad); !valid)
        return valid;
      if (type.width() == 2 && frame.locals[index + 1].kind != Kind::WideContinuation)
        return fail(AnalysisErrorKind::TypeMismatch, "category-two local is missing its second slot");
      return push(frame, value);
    }
    case Opcode::IStore:
    case Opcode::LStore:
    case Opcode::FStore:
    case Opcode::DStore:
    case Opcode::AStore: {
      const auto type = LOCAL_TYPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::IStore)];
      auto value = pop_matching(frame, type, opcode == Opcode::AStore);
      if (!value)
        return std::unexpected(std::move(value.error()));
      store_local(frame, *instruction.local_index(), *value); // NOLINT(bugprone-unchecked-optional-access)
      return {};
    }
    case Opcode::IInc:
      return match(frame.locals[instruction.increment_value()->local], INT); // NOLINT(bugprone-unchecked-optional-access)
    case Opcode::IALoad:
    case Opcode::LALoad:
    case Opcode::FALoad:
    case Opcode::DALoad:
    case Opcode::AALoad:
    case Opcode::BALoad:
    case Opcode::CALoad:
    case Opcode::SALoad:
    case Opcode::IAStore:
    case Opcode::LAStore:
    case Opcode::FAStore:
    case Opcode::DAStore:
    case Opcode::AAStore:
    case Opcode::BAStore:
    case Opcode::CAStore:
    case Opcode::SAStore:
      return execute_array_access(frame, opcode);
    case Opcode::Pop:
    case Opcode::Pop2:
    case Opcode::Dup:
    case Opcode::DupX1:
    case Opcode::DupX2:
    case Opcode::Dup2:
    case Opcode::Dup2X1:
    case Opcode::Dup2X2:
    case Opcode::Swap:
      return manipulate_stack(frame, opcode);
    case Opcode::IAdd:
    case Opcode::LAdd:
    case Opcode::FAdd:
    case Opcode::DAdd:
    case Opcode::ISub:
    case Opcode::LSub:
    case Opcode::FSub:
    case Opcode::DSub:
    case Opcode::IMul:
    case Opcode::LMul:
    case Opcode::FMul:
    case Opcode::DMul:
    case Opcode::IDiv:
    case Opcode::LDiv:
    case Opcode::FDiv:
    case Opcode::DDiv:
    case Opcode::IRem:
    case Opcode::LRem:
    case Opcode::FRem:
    case Opcode::DRem: {
      const auto type = NUMERIC_TYPES[(static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::IAdd)) % NUMERIC_TYPES.size()];
      return transition(frame, {type, type}, type);
    }
    case Opcode::INeg:
    case Opcode::LNeg:
    case Opcode::FNeg:
    case Opcode::DNeg: {
      const auto type = NUMERIC_TYPES[static_cast<unsigned>(opcode) - static_cast<unsigned>(Opcode::INeg)];
      return transition(frame, {type}, type);
    }
    case Opcode::IShl:
    case Opcode::IShr:
    case Opcode::IUShr:
    case Opcode::IAnd:
    case Opcode::IOr:
    case Opcode::IXor:
      return transition(frame, {INT, INT}, INT);
    case Opcode::LShl:
    case Opcode::LShr:
    case Opcode::LUShr:
      return transition(frame, {INT, LONG}, LONG);
    case Opcode::LAnd:
    case Opcode::LOr:
    case Opcode::LXor:
      return transition(frame, {LONG, LONG}, LONG);
    case Opcode::I2L:
      return transition(frame, {INT}, LONG);
    case Opcode::I2F:
      return transition(frame, {INT}, FLOAT);
    case Opcode::I2D:
      return transition(frame, {INT}, DOUBLE);
    case Opcode::L2I:
      return transition(frame, {LONG}, INT);
    case Opcode::L2F:
      return transition(frame, {LONG}, FLOAT);
    case Opcode::L2D:
      return transition(frame, {LONG}, DOUBLE);
    case Opcode::F2I:
      return transition(frame, {FLOAT}, INT);
    case Opcode::F2L:
      return transition(frame, {FLOAT}, LONG);
    case Opcode::F2D:
      return transition(frame, {FLOAT}, DOUBLE);
    case Opcode::D2I:
      return transition(frame, {DOUBLE}, INT);
    case Opcode::D2L:
      return transition(frame, {DOUBLE}, LONG);
    case Opcode::D2F:
      return transition(frame, {DOUBLE}, FLOAT);
    case Opcode::I2B:
    case Opcode::I2C:
    case Opcode::I2S:
      return transition(frame, {INT}, INT);
    case Opcode::LCmp:
      return transition(frame, {LONG, LONG}, INT);
    case Opcode::FCmpL:
    case Opcode::FCmpG:
      return transition(frame, {FLOAT, FLOAT}, INT);
    case Opcode::DCmpL:
    case Opcode::DCmpG:
      return transition(frame, {DOUBLE, DOUBLE}, INT);
    case Opcode::IfEq:
    case Opcode::IfNe:
    case Opcode::IfLt:
    case Opcode::IfGe:
    case Opcode::IfGt:
    case Opcode::IfLe:
    case Opcode::TableSwitch:
    case Opcode::LookupSwitch:
      return transition(frame, {INT});
    case Opcode::IfICmpEq:
    case Opcode::IfICmpNe:
    case Opcode::IfICmpLt:
    case Opcode::IfICmpGe:
    case Opcode::IfICmpGt:
    case Opcode::IfICmpLe:
      return transition(frame, {INT, INT});
    case Opcode::IfACmpEq:
    case Opcode::IfACmpNe:
      if (auto value = pop_matching(frame, REFERENCE, true); !value)
        return std::unexpected(std::move(value.error()));
      [[fallthrough]];
    case Opcode::IfNull:
    case Opcode::IfNonNull:
    case Opcode::MonitorEnter:
    case Opcode::MonitorExit: {
      auto value = pop_matching(frame, REFERENCE, true);
      return value ? Status{} : std::unexpected(std::move(value.error()));
    }
    case Opcode::IReturn:
    case Opcode::LReturn:
    case Opcode::FReturn:
    case Opcode::DReturn:
    case Opcode::AReturn:
    case Opcode::Return:
      return execute_return(frame, opcode);
    case Opcode::NewArray: {
      constexpr std::array PRIMITIVE_ARRAYS{"[Z", "[C", "[F", "[D", "[B", "[S", "[I", "[J"};
      const auto atype = static_cast<unsigned>(*instruction.array_type()); // NOLINT(bugprone-unchecked-optional-access)
      if (atype < 4 || atype > 11)
        return fail(AnalysisErrorKind::InvalidCode, "invalid newarray component type");
      return transition(frame, {INT}, object(PRIMITIVE_ARRAYS[atype - 4]));
    }
    case Opcode::ArrayLength: {
      auto array = pop_matching(frame, REFERENCE);
      if (!array)
        return std::unexpected(std::move(array.error()));
      if (hierarchy && array->kind != Kind::Null && !array->reference.starts_with('['))
        return fail(AnalysisErrorKind::TypeMismatch, "arraylength requires an array, found " + type_name(*array));
      return push(frame, INT);
    }
    case Opcode::AThrow:
      if (auto exception = pop_matching(frame, THROWABLE); !exception)
        return std::unexpected(std::move(exception.error()));
      clear_stack(frame);
      return {};
    case Opcode::Jsr:
    case Opcode::Ret:
      return fail(AnalysisErrorKind::UnsupportedSubroutineAnalysis, "subroutine instructions need return-address analysis");
    default:
      return fail(AnalysisErrorKind::InvalidCode, "unsupported logical opcode");
  }
}

std::expected<std::optional<VerificationType>, AnalysisError>
InstructionEffects::apply(Frame& frame, InstructionId at, Instruction instruction) {
  std::optional<VerificationType> initialized;
  if (auto status = execute(frame, at, instruction, initialized); !status) {
    status.error().instruction = at;
    return std::unexpected(std::move(status.error()));
  }
  return initialized;
}

} // namespace cafe::analysis
