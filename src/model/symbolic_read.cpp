/**
 * @file
 * @brief Locate and decode symbolic operands from class-owned bytecode.
 * @ingroup implementation
 */

#include <cafe/class_file.hpp>

#include <bit>
#include <unordered_map>

#include "io/class_format.hpp"
#include "io/code_operands.hpp"

namespace cafe {
namespace {

/// A symbolic read result with the instruction API's error type.
template<typename T>
using Result = std::expected<T, CodeEditError>;

/**
 * @brief Borrow a constant's decoded symbolic name and descriptor.
 * @ingroup implementation
 */
struct SignatureText {
  std::string_view name; ///< Unqualified name in modified UTF-8.
  std::string_view descriptor; ///< JVM descriptor in modified UTF-8.
  friend bool operator==(SignatureText, SignatureText) = default;
};

/**
 * @brief Borrow the complete symbolic target of a member access.
 * @ingroup implementation
 */
struct MemberReference {
  std::string_view owner; ///< Internal owner name or array descriptor.
  SignatureText signature; ///< Member name and descriptor.
  bool owner_is_interface = false; ///< True for InterfaceMethodref.
};

/**
 * @brief Detect the four field access opcodes.
 * @param opcode Logical instruction opcode.
 * @return True when its operand is a field reference.
 */
bool field_access(Opcode opcode) noexcept {
  return opcode >= Opcode::GetStatic && opcode <= Opcode::PutField;
}

/**
 * @brief Detect the four ordinary invocation opcodes.
 * @param opcode Logical instruction opcode.
 * @return True when its operand is a class or interface method reference.
 */
bool method_invocation(Opcode opcode) noexcept {
  return opcode >= Opcode::InvokeVirtual && opcode <= Opcode::InvokeInterface;
}

/**
 * @brief Read validated modified UTF-8 text with errors in the symbolic read domain.
 * @param pool Pool owning the text.
 * @param index Index of its Utf8 record.
 * @return Borrowed text or an invalid operand.
 */
Result<std::string_view> read_text(const ConstantPool& pool, CpIndex index) noexcept {
  return pool.get_utf8(index).transform_error([](CpError) { return CodeEditError::InvalidOperand; });
}

/**
 * @brief Borrow the symbolic name and descriptor behind a constant's NameAndType reference.
 * @param pool Pool owning the referenced NameAndType and text.
 * @param entry Field reference, method reference, or dynamic entry.
 * @param version Class version for reference validation.
 * @return Name and descriptor, or an invalid operand.
 */
Result<SignatureText> read_signature(const ConstantPool& pool, const CpEntry& entry, ClassVersion version) noexcept {
  if (detail::cp_reference_issue(pool, entry, version, false))
    return std::unexpected(CodeEditError::InvalidOperand);
  const auto* signature = pool.get(entry.second);
  if (!signature || signature->tag != CpTag::NameAndType)
    return std::unexpected(CodeEditError::InvalidOperand);
  const auto name = read_text(pool, signature->first);
  const auto descriptor = read_text(pool, signature->second);
  if (!name || !descriptor)
    return std::unexpected(CodeEditError::InvalidOperand);
  return SignatureText{*name, *descriptor};
}

/**
 * @brief Decode a member reference after its opcode or method handle has been validated.
 * @param pool Pool owning the reference.
 * @param entry Field or method reference.
 * @param version Class version for reference validation.
 * @return Complete symbolic target, or an invalid operand.
 */
Result<MemberReference> read_member(const ConstantPool& pool, const CpEntry& entry, ClassVersion version) noexcept {
  const auto signature = read_signature(pool, entry, version);
  const auto* owner = pool.get(entry.first);
  if (!signature || !owner || detail::cp_reference_issue(pool, *owner, version, false))
    return std::unexpected(CodeEditError::InvalidOperand);
  const auto name = read_text(pool, owner->first);
  if (!name)
    return std::unexpected(name.error());
  return MemberReference{*name, *signature, entry.tag == CpTag::InterfaceMethodref};
}

/**
 * @brief Look up and validate the pool operand for an instruction with a symbolic form.
 * @param file Class owning the pool.
 * @param instruction Field access, invocation, or literal load.
 * @return Borrowed pool entry or an invalid operand.
 */
Result<const CpEntry*> checked_operand(const ClassFile& file, Instruction instruction) {
  const auto& pool = file.constant_pool();
  const auto index = instruction.constant_index();
  const auto* entry = index ? pool.get(*index) : nullptr;
  if (!entry || detail::validate_instruction(instruction, file.version(), pool, 0))
    return std::unexpected(CodeEditError::InvalidOperand);
  return entry;
}

/**
 * @brief Search decoded methods in declaration and instruction order.
 * @param file Class to search.
 * @param method_index Optional restriction to one method table index.
 * @param invocations Select ordinary invocations when true and field accesses otherwise.
 * @param target Exact modified UTF-8 owner, name, and descriptor.
 * @return Matching locations, or an invalid method or operand.
 */
Result<std::vector<InstructionLocation>> find_references(const ClassFile& file, std::optional<std::size_t> method_index,
                                                         bool invocations, const MemberReference& target) {
  const auto methods = file.methods();
  if (method_index && *method_index >= methods.size())
    return std::unexpected(CodeEditError::InvalidMethod);
  const auto first = method_index.value_or(0);
  const auto end = method_index ? first + 1 : methods.size();
  const auto matches_opcode = invocations ? method_invocation : field_access;
  std::vector<InstructionLocation> matches;
  for (auto index = first; index < end; ++index) {
    const auto* code = methods[index].code();
    if (!code)
      continue;
    for (const auto& instruction : code->code().instructions()) {
      if (!matches_opcode(instruction.instruction.opcode()))
        continue;
      const auto operand = checked_operand(file, instruction.instruction);
      if (!operand)
        return std::unexpected(operand.error());
      const auto member = read_member(file.constant_pool(), **operand, file.version());
      if (!member)
        return std::unexpected(member.error());
      if (member->owner == target.owner && member->signature == target.signature)
        matches.push_back({index, instruction.id});
    }
  }
  return matches;
}

/**
 * @brief Decode the reachable bootstrap graph, allocating one argument buffer per row.
 * @ingroup implementation
 */
class BootstrapReader {
public:
  /**
   * @brief Borrow the class and the result's argument storage.
   * @param file Class supplying symbolic operands and its bootstrap table.
   * @param buffers Container backing the returned bootstrap spans.
   */
  BootstrapReader(const ClassFile& file, std::vector<std::vector<BootstrapArgument>>& buffers) : pool(file.constant_pool()), version(file.version()), attributes(file.attributes()), buffers(buffers) {
  }

  /**
   * @brief Decode a root bootstrap and all reachable argument rows without recursion.
   * @param ordinal Root bootstrap table ordinal.
   * @return The root bootstrap and argument view, or an invalid operand.
   */
  Result<Bootstrap> read(CpIndex ordinal) {
    const BootstrapMethods* table = nullptr;
    for (const auto& attribute : attributes) {
      if (pool.get_utf8(attribute.name_index()) != "BootstrapMethods")
        continue;
      const auto* candidate = attribute.get<BootstrapMethods>();
      if (table || !candidate)
        return std::unexpected(CodeEditError::InvalidOperand);
      table = candidate;
    }
    if (!table)
      return std::unexpected(CodeEditError::InvalidOperand);
    rows = table->methods;

    const auto root = refer_to_row(ordinal);
    if (!root)
      return std::unexpected(root.error());
    for (std::size_t i = 0; i < pending.size(); ++i) {
      // Nested rows may grow both vectors. The inner argument buffers retain their addresses.
      const auto [source, destination] = pending[i];
      for (std::size_t j = 0; j < source.size(); ++j) {
        auto argument = read_argument(source[j]);
        if (!argument)
          return std::unexpected(argument.error());
        destination[j] = *argument;
      }
    }
    return root;
  }

private:
  /**
   * @brief A reachable row whose argument buffer has yet to be filled.
   * @ingroup implementation
   */
  struct PendingRow {
    std::span<const CpIndex> source; ///< Original ordered bootstrap argument indices.
    std::span<BootstrapArgument> destination; ///< Stable result buffer for this row.
  };

  /**
   * @brief Decode a method handle and its symbolic target.
   * @param index Pool index naming a MethodHandle.
   * @return Handle kind and target, or an invalid operand.
   */
  Result<MethodHandle> read_handle(CpIndex index) const {
    const auto* handle = pool.get(index);
    if (!handle || handle->tag != CpTag::MethodHandle ||
        detail::cp_reference_issue(pool, *handle, version, false))
      return std::unexpected(CodeEditError::InvalidOperand);
    const auto* reference = pool.get(handle->first);
    if (!reference)
      return std::unexpected(CodeEditError::InvalidOperand);
    const auto member = read_member(pool, *reference, version);
    if (!member)
      return std::unexpected(member.error());
    return MethodHandle{static_cast<MethodHandleKind>(handle->reference_kind), member->owner,
                        member->signature.name, member->signature.descriptor, member->owner_is_interface};
  }

  /**
   * @brief Get one row's symbolic view and queue its argument decoding the first time it is seen.
   * @param ordinal Zero-based row index.
   * @return Symbolic bootstrap view, or an invalid ordinal or handle.
   */
  Result<Bootstrap> refer_to_row(CpIndex ordinal) {
    if (ordinal.value >= rows.size())
      return std::unexpected(CodeEditError::InvalidOperand);
    if (const auto found = decoded.find(ordinal.value); found != decoded.end())
      return found->second;

    const auto& row = rows[ordinal.value];
    const auto handle = read_handle(row.method_handle);
    if (!handle)
      return std::unexpected(handle.error());
    std::span<BootstrapArgument> arguments;
    if (!row.arguments.empty()) {
      arguments = buffers.emplace_back(row.arguments.size());
      pending.push_back({row.arguments, arguments});
    }
    const Bootstrap bootstrap{*handle, arguments};
    decoded.emplace(ordinal.value, bootstrap);
    return bootstrap;
  }

  /**
   * @brief Decode a loadable bootstrap argument, queuing nested dynamic bootstrap rows as needed.
   * @param index Pool index naming the argument.
   * @return The symbolic argument or an invalid operand.
   */
  Result<BootstrapArgument> read_argument(CpIndex index) {
    const auto* entry = pool.get(index);
    if (!entry || !detail::loadable_constant(entry->tag, version) ||
        detail::cp_reference_issue(pool, *entry, version, false))
      return std::unexpected(CodeEditError::InvalidOperand);
    switch (entry->tag) {
      case CpTag::Integer:
        return BootstrapArgument{std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(entry->bits))};
      case CpTag::Float:
        return BootstrapArgument{std::bit_cast<float>(static_cast<std::uint32_t>(entry->bits))};
      case CpTag::Long:
        return BootstrapArgument{std::bit_cast<std::int64_t>(entry->bits)};
      case CpTag::Double:
        return BootstrapArgument{std::bit_cast<double>(entry->bits)};
      case CpTag::String:
        return read_text(pool, entry->first).transform([](std::string_view text) { return BootstrapArgument{text}; });
      case CpTag::Class:
        return read_text(pool, entry->first).transform([](std::string_view text) {
          return BootstrapArgument{ClassConstant{text}};
        });
      case CpTag::MethodType:
        return read_text(pool, entry->first).transform([](std::string_view text) {
          return BootstrapArgument{MethodTypeConstant{text}};
        });
      case CpTag::MethodHandle:
        return read_handle(index).transform([](MethodHandle handle) { return BootstrapArgument{handle}; });
      case CpTag::Dynamic: {
        const auto signature = read_signature(pool, *entry, version);
        if (!signature)
          return std::unexpected(signature.error());
        return refer_to_row(entry->first).transform([&](Bootstrap bootstrap) {
          return BootstrapArgument{DynamicConstant{signature->name, signature->descriptor, bootstrap}};
        });
      }
      default:
        return std::unexpected(CodeEditError::InvalidOperand);
    }
  }

  const ConstantPool& pool; ///< Source of symbolic texts and references.
  ClassVersion version; ///< Version used to validate tags and references.
  std::span<const Attribute> attributes; ///< Class attributes searched for the bootstrap table.
  std::vector<std::vector<BootstrapArgument>>& buffers; ///< Result-owned bootstrap argument buffers.
  std::span<const BootstrapMethod> rows; ///< Borrowed bootstrap records, established before traversal.
  std::unordered_map<std::uint16_t, Bootstrap> decoded; ///< Row views reserved before visiting arguments.
  std::vector<PendingRow> pending; ///< Rows to fill in discovery order.
};

} // namespace

std::expected<std::vector<InstructionLocation>, CodeEditError> ClassFile::find_invocations(
    std::string_view owner, std::string_view name, std::string_view descriptor,
    std::optional<std::size_t> method_index) const {
  return find_references(*this, method_index, true, {owner, {name, descriptor}});
}

std::expected<std::vector<InstructionLocation>, CodeEditError> ClassFile::find_field_accesses(
    std::string_view owner, std::string_view name, std::string_view descriptor,
    std::optional<std::size_t> method_index) const {
  return find_references(*this, method_index, false, {owner, {name, descriptor}});
}

std::expected<SymbolicInstruction, CodeEditError> ClassFile::inspect_instruction(InstructionLocation location) const {
  if (location.method_index >= methods().size())
    return std::unexpected(CodeEditError::InvalidMethod);
  const auto* code = methods()[location.method_index].code();
  if (!code)
    return std::unexpected(CodeEditError::NoCode);
  const auto instruction = code->code().get(location.id);
  if (!instruction)
    return std::unexpected(instruction.error());

  const auto opcode = instruction->opcode();
  const bool field = field_access(opcode);
  const bool method = method_invocation(opcode);
  const bool indy = opcode == Opcode::InvokeDynamic;
  if (!field && !method && !indy && opcode != Opcode::Ldc && opcode != Opcode::Ldc2W)
    return std::unexpected(CodeEditError::WrongOperandForm);

  const auto operand = checked_operand(*this, *instruction);
  if (!operand)
    return std::unexpected(operand.error());
  const auto& entry = **operand;
  if (field || method) {
    const auto member = read_member(constant_pool(), entry, version());
    if (!member)
      return std::unexpected(member.error());
    if (field)
      return SymbolicInstruction(FieldAccess{opcode, member->owner, member->signature.name, member->signature.descriptor});
    return SymbolicInstruction(MethodInvocation{opcode, member->owner, member->signature.name, member->signature.descriptor,
                                                member->owner_is_interface});
  }
  if (!indy && entry.tag != CpTag::Dynamic)
    return std::unexpected(CodeEditError::WrongOperandForm);

  const auto signature = read_signature(constant_pool(), entry, version());
  if (!signature)
    return std::unexpected(signature.error());
  std::vector<std::vector<BootstrapArgument>> arguments;
  BootstrapReader reader(*this, arguments);
  const auto bootstrap = reader.read(entry.first);
  if (!bootstrap)
    return std::unexpected(bootstrap.error());
  if (indy)
    return SymbolicInstruction(DynamicInvocation{signature->name, signature->descriptor, *bootstrap}, std::move(arguments));
  return SymbolicInstruction(DynamicConstant{signature->name, signature->descriptor, *bootstrap}, std::move(arguments));
}

} // namespace cafe
