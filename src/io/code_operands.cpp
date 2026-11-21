/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include "code_operands.hpp"

#include "detail/descriptor.hpp"

namespace cafe::detail {
namespace {

/**
 * @brief Follow a validated member or dynamic reference to its descriptor.
 * @param pool Owning class constant pool used for reference validation.
 * @param index One-based constant-pool reference; the required tag is determined by this
 *   operation.
 * @return The borrowed descriptor when the expected constant chain exists, otherwise nullopt.
 * @ingroup implementation
 */
std::optional<std::string_view> nested_descriptor(const ConstantPool& pool, CpIndex index) {
  auto name_and_type = pool.get(index);
  if (!name_and_type || name_and_type->tag != CpTag::NameAndType)
    return std::nullopt;
  auto descriptor = pool.get_utf8(name_and_type->second);
  if (!descriptor)
    return std::nullopt;
  return *descriptor;
}

/**
 * @brief Check whether a constant tag is loadable by this ldc category and version.
 * @param tag Required constant-pool tag.
 * @param category_two Whether the load requires a long/double two-slot constant.
 * @param version Exact JVM class-file major and minor version.
 * @return True for a legal category-one or category-two load.
 * @ingroup implementation
 */
bool valid_ldc(CpTag tag, bool category_two, ClassVersion version) {
  if (category_two)
    return tag == CpTag::Long || tag == CpTag::Double;
  return tag == CpTag::Integer || tag == CpTag::Float || tag == CpTag::String ||
         (tag == CpTag::Class && version.major >= 49) ||
         ((tag == CpTag::MethodType || tag == CpTag::MethodHandle) && version.major >= 51);
}

/**
 * @brief Check an instruction's constant tag, descriptor, and version-dependent operand
 * restrictions.
 * @param instruction Logical instruction value; scoped operands require the owning method.
 * @param version Exact JVM class-file major and minor version.
 * @param pool Owning class constant pool used for reference validation.
 * @return An operand error when invalid, or an empty optional when valid.
 * @ingroup implementation
 */
// Instruction factories guarantee the opcode-specific operand; descriptor accesses follow descriptor validation.
std::optional<OperandError> validate_constant(Instruction instruction, ClassVersion version,
                                              const ConstantPool& pool) {
  auto cp = pool.get(*instruction.constant_index()); // NOLINT(bugprone-unchecked-optional-access)
  if (!cp)
    return OperandError{"constant pool index does not name an entry", true};

  const auto check = [](bool valid, std::string_view reason) -> std::optional<OperandError> {
    if (!valid)
      return OperandError{reason, true};
    return std::nullopt;
  };
  switch (instruction.opcode()) {
    case Opcode::Ldc:
    case Opcode::Ldc2W: {
      auto category_two = instruction.opcode() == Opcode::Ldc2W;
      if (cp->tag != CpTag::Dynamic)
        return check(valid_ldc(cp->tag, category_two, version), "constant is not loadable by this opcode and class version");
      auto descriptor = nested_descriptor(pool, cp->second);
      if (version.major < 55 || !descriptor)
        return OperandError{"invalid dynamic constant reference", true};
      auto width = consume_field_type(*descriptor);
      return check(width == (category_two ? 2u : 1u) && descriptor->empty(),
                   "dynamic constant descriptor has the wrong category");
    }
    case Opcode::GetStatic:
    case Opcode::PutStatic:
    case Opcode::GetField:
    case Opcode::PutField:
      return check(cp->tag == CpTag::Fieldref, "expected a field reference");
    case Opcode::InvokeVirtual:
    case Opcode::InvokeSpecial:
    case Opcode::InvokeStatic:
    case Opcode::InvokeInterface: {
      auto opcode = instruction.opcode();
      bool interface_allowed = (opcode == Opcode::InvokeSpecial || opcode == Opcode::InvokeStatic) && version.major >= 52;
      bool tag_ok = opcode == Opcode::InvokeInterface ? cp->tag == CpTag::InterfaceMethodref
                                                      : cp->tag == CpTag::Methodref || (interface_allowed && cp->tag == CpTag::InterfaceMethodref);
      if (!tag_ok)
        return OperandError{"wrong method reference tag for invocation", true};
      auto name_and_type = pool.get(cp->second);
      if (!name_and_type || name_and_type->tag != CpTag::NameAndType)
        return OperandError{"method reference has an invalid name-and-type index", true};
      auto name = pool.get_utf8(name_and_type->first);
      if (!name || *name == "<clinit>" || (*name == "<init>" && opcode != Opcode::InvokeSpecial))
        return OperandError{"invocation refers to an invalid method name", true};
      auto descriptor = pool.get_utf8(name_and_type->second);
      auto slots = descriptor ? method_parameter_slots(*descriptor) : std::nullopt;
      if (!slots || (*slots == 255 && opcode != Opcode::InvokeStatic))
        return OperandError{"method reference has an invalid descriptor or parameter slot count", true};
      if (opcode == Opcode::InvokeInterface && *slots + 1 != *instruction.interface_count()) // NOLINT(bugprone-unchecked-optional-access)
        return OperandError{"invokeinterface count does not match the descriptor slot count", false};
      return std::nullopt;
    }
    case Opcode::InvokeDynamic:
      return check(cp->tag == CpTag::InvokeDynamic, "expected an invokedynamic reference");
    case Opcode::New:
    case Opcode::ANewArray:
    case Opcode::CheckCast:
    case Opcode::InstanceOf:
    case Opcode::MultiANewArray: {
      if (cp->tag != CpTag::Class)
        return OperandError{"expected a class reference", true};
      auto class_name = pool.get_utf8(cp->first);
      if (!class_name)
        return OperandError{"class reference has an invalid name", true};
      if (instruction.opcode() == Opcode::New)
        return check(!class_name->starts_with('['), "new requires a non-array class");
      if (instruction.opcode() == Opcode::MultiANewArray) {
        auto dimensions = class_name->find_first_not_of('[');
        if (dimensions == std::string_view::npos || dimensions == 0 || !valid_field_descriptor(*class_name))
          return OperandError{"multianewarray requires an array class descriptor", true};
        auto count = *instruction.dimensions(); // NOLINT(bugprone-unchecked-optional-access)
        if (count == 0 || count > dimensions)
          return OperandError{"multianewarray dimensions exceed array rank", false};
      }
      if (instruction.opcode() == Opcode::ANewArray && class_name->starts_with('[') &&
          class_name->find_first_not_of('[') >= 255)
        return OperandError{"anewarray would produce an array with more than 255 dimensions", false};
      return std::nullopt;
    }
    default:
      return OperandError{"unexpected constant operand", false};
  }
}

} // namespace

// Opcode-specific accessors cannot be empty for the corresponding immutable Instruction form.
std::optional<OperandError> validate_instruction(Instruction instruction, ClassVersion version,
                                                 const ConstantPool& pool, std::uint16_t max_locals) {
  const auto& info = opcode_info(instruction.opcode());
  if (!info.is_allowed(version.major) || info.logical_opcode != instruction.opcode())
    return OperandError{"opcode is invalid for the declared class version", false};

  switch (info.form) {
    case OperandForm::Local:
    case OperandForm::Increment: {
      auto index = info.form == OperandForm::Local ? *instruction.local_index() : instruction.increment_value()->local; // NOLINT(bugprone-unchecked-optional-access)
      if (static_cast<unsigned>(index) + info.local_width > max_locals)
        return OperandError{"local variable operand exceeds max_locals", false};
      return std::nullopt;
    }
    case OperandForm::ConstantByte:
    case OperandForm::ConstantShort:
    case OperandForm::InvokeInterface:
    case OperandForm::InvokeDynamic:
    case OperandForm::MultiANewArray:
      return validate_constant(instruction, version, pool);
    default:
      return std::nullopt;
  }
}

} // namespace cafe::detail
