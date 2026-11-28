/**
 * @file
 * @brief Class-aware traversal and diagnostic formatting of JVM instructions.
 * @ingroup implementation
 */

#include <cafe/class_file.hpp>

#include "model/class_storage.hpp"

#include <array>
#include <locale>
#include <sstream>

#include "code_access.hpp"
#include "instruction_format.hpp"

namespace cafe::detail {

void write_opcode(std::ostream& out, Opcode opcode) {
  // JVM encodings from nop through breakpoint form a contiguous range.
  // clang-format off
  static constexpr auto names = std::to_array<std::string_view>({
    "nop", "aconst_null", "iconst_m1", "iconst_0", "iconst_1", "iconst_2", "iconst_3", "iconst_4",
    "iconst_5", "lconst_0", "lconst_1", "fconst_0", "fconst_1", "fconst_2", "dconst_0", "dconst_1",
    "bipush", "sipush", "ldc", "ldc_w", "ldc2_w", "iload", "lload", "fload",
    "dload", "aload", "iload_0", "iload_1", "iload_2", "iload_3", "lload_0", "lload_1",
    "lload_2", "lload_3", "fload_0", "fload_1", "fload_2", "fload_3", "dload_0", "dload_1",
    "dload_2", "dload_3", "aload_0", "aload_1", "aload_2", "aload_3", "iaload", "laload",
    "faload", "daload", "aaload", "baload", "caload", "saload", "istore", "lstore",
    "fstore", "dstore", "astore", "istore_0", "istore_1", "istore_2", "istore_3", "lstore_0",
    "lstore_1", "lstore_2", "lstore_3", "fstore_0", "fstore_1", "fstore_2", "fstore_3", "dstore_0",
    "dstore_1", "dstore_2", "dstore_3", "astore_0", "astore_1", "astore_2", "astore_3", "iastore",
    "lastore", "fastore", "dastore", "aastore", "bastore", "castore", "sastore", "pop",
    "pop2", "dup", "dup_x1", "dup_x2", "dup2", "dup2_x1", "dup2_x2", "swap",
    "iadd", "ladd", "fadd", "dadd", "isub", "lsub", "fsub", "dsub",
    "imul", "lmul", "fmul", "dmul", "idiv", "ldiv", "fdiv", "ddiv",
    "irem", "lrem", "frem", "drem", "ineg", "lneg", "fneg", "dneg",
    "ishl", "lshl", "ishr", "lshr", "iushr", "lushr", "iand", "land",
    "ior", "lor", "ixor", "lxor", "iinc", "i2l", "i2f", "i2d",
    "l2i", "l2f", "l2d", "f2i", "f2l", "f2d", "d2i", "d2l",
    "d2f", "i2b", "i2c", "i2s", "lcmp", "fcmpl", "fcmpg", "dcmpl",
    "dcmpg", "ifeq", "ifne", "iflt", "ifge", "ifgt", "ifle", "if_icmpeq",
    "if_icmpne", "if_icmplt", "if_icmpge", "if_icmpgt", "if_icmple", "if_acmpeq", "if_acmpne", "goto",
    "jsr", "ret", "tableswitch", "lookupswitch", "ireturn", "lreturn", "freturn", "dreturn",
    "areturn", "return", "getstatic", "putstatic", "getfield", "putfield", "invokevirtual", "invokespecial",
    "invokestatic", "invokeinterface", "invokedynamic", "new", "newarray", "anewarray", "arraylength", "athrow",
    "checkcast", "instanceof", "monitorenter", "monitorexit", "wide", "multianewarray", "ifnull", "ifnonnull",
    "goto_w", "jsr_w", "breakpoint",
  });
  // clang-format on
  static_assert(names.size() == static_cast<unsigned>(Opcode::Breakpoint) + 1);
  const auto code = static_cast<unsigned>(opcode);
  if (code < names.size())
    out << names[code];
  else if (opcode == Opcode::Impdep1 || opcode == Opcode::Impdep2)
    out << (opcode == Opcode::Impdep1 ? "impdep1" : "impdep2");
  else
    out << "opcode(" << code << ')';
}

void write_raw_operands(std::ostream& out, const Instruction& instruction) {
  if (const auto value = instruction.immediate_value())
    out << ' ' << *value;
  if (const auto slot = instruction.local_index())
    out << " local[" << *slot << ']';
  if (const auto inc = instruction.increment_value())
    out << " local[" << inc->local << "] " << inc->amount;
  if (const auto index = instruction.constant_index())
    out << " #" << index->value;
  if (const auto type = instruction.array_type())
    out << " atype " << static_cast<unsigned>(*type);
  if (const auto dimensions = instruction.dimensions())
    out << " dimensions " << static_cast<unsigned>(*dimensions);
}

} // namespace cafe::detail

namespace cafe {
namespace {

/// Borrow the method code used by both checked traversal and stable-handle lookup.
/// @param file Class supplying method declarations.
/// @param method_index Source method ordinal.
/// @return Decoded code, or InvalidMethod/NoCode.
std::expected<const MethodCode*, CodeEditError> decoded_code(const ClassFile& file, std::size_t method_index) noexcept {
  const auto methods = file.methods();
  if (method_index >= methods.size())
    return std::unexpected(CodeEditError::InvalidMethod);
  const auto* attribute = methods[method_index].code();
  if (!attribute)
    return std::unexpected(CodeEditError::NoCode);
  return &attribute->code();
}

} // namespace

InstructionView::InstructionView(const detail::ClassStorage* storage, std::size_t method_index,
                                 InstructionEntry entry) noexcept :
    storage(storage), pool_revision(storage->pool_revision), method_index(method_index), entry(entry) {
}

Instruction InstructionView::raw() const noexcept {
  if (pool_revision != storage->pool_revision && entry.instruction.constant_index())
    return detail::BytecodeAccess::with_constant_index(entry.instruction, {});
  return entry.instruction;
}

InstructionViewRange ClassFile::instructions(std::size_t method_index) const noexcept {
  return checked_instructions(method_index).value_or(InstructionViewRange{});
}

std::size_t MethodEditor::method_index() const noexcept {
  return static_cast<std::size_t>(member - storage->method_entries.data());
}

InstructionViewRange MethodEditor::instructions() const noexcept {
  const auto* body = code();
  if (!body)
    return {};
  const auto index = method_index();
  const auto entries = body->code().instructions();
  return {InstructionViewIterator(storage, index, entries.begin()),
          InstructionViewIterator(storage, index, entries.end()), entries.size()};
}

std::expected<InstructionView, CodeEditError> MethodEditor::instruction(InstructionId id) const noexcept {
  const auto* body = code();
  if (!body)
    return std::unexpected(CodeEditError::NoCode);
  return body->code().get(id).transform([&](Instruction value) { return InstructionView(storage, method_index(), {id, value}); });
}

std::vector<CodeNode> MethodEditor::nodes() const {
  const auto* body = code();
  return body ? body->code().nodes() : std::vector<CodeNode>{};
}

std::expected<InstructionViewRange, CodeEditError> ClassFile::checked_instructions(std::size_t method_index) const noexcept {
  const auto body = decoded_code(*this, method_index);
  if (!body)
    return std::unexpected(body.error());
  const auto entries = (*body)->instructions();
  return InstructionViewRange{InstructionViewIterator(storage.get(), method_index, entries.begin()),
                              InstructionViewIterator(storage.get(), method_index, entries.end()), entries.size()};
}

std::expected<InstructionView, CodeEditError> ClassFile::instruction(InstructionLocation location) const noexcept {
  const auto body = decoded_code(*this, location.method_index);
  if (!body)
    return std::unexpected(body.error());
  const auto value = (*body)->get(location.id);
  if (!value)
    return std::unexpected(value.error());
  return InstructionView(storage.get(), location.method_index, {location.id, *value});
}

std::string format(InstructionView instruction) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  detail::write_opcode(out, instruction.opcode());
  if (instruction.is_invoke()) {
    if (const auto call = instruction.method_invocation()) {
      out << ' ' << call->owner << '.' << call->name << ' ' << call->descriptor;
      return std::move(out).str();
    }
  }
  if (instruction.is_field_access()) {
    if (const auto field = instruction.field_access()) {
      out << ' ' << field->owner << '.' << field->name << ' ' << field->descriptor;
      return std::move(out).str();
    }
  }
  detail::write_raw_operands(out, instruction.raw());
  return std::move(out).str();
}

} // namespace cafe
