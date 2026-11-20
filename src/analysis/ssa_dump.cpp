/**
 * @file
 * @brief Human-readable rendering of SSA definitions and control flow.
 * @ingroup implementation
 */
#include <algorithm>
#include <array>
#include <locale>
#include <sstream>
#include "ssa_detail.hpp"

namespace cafe::analysis {
namespace {

/**
 * @brief Print the JVM spelling of an operation, including reserved encodings for diagnostics.
 * @param out Destination stream.
 * @param opcode Bytecode operation to display.
 */
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

/**
 * @brief Render a typed definition as it appears at a parameter, phi, exception or instruction.
 * @param out Destination stream.
 * @param value Definition with a graph-local identity.
 */
void write_definition(std::ostream& out, const SsaValue& value) {
  out << 'v' << value.id.index << ": ";
  const auto& type = value.type;
  switch (type.kind) {
    case VerificationKind::Top:
      out << "top";
      break;
    case VerificationKind::Integer:
      out << "int";
      break;
    case VerificationKind::Float:
      out << "float";
      break;
    case VerificationKind::Long:
      out << "long";
      break;
    case VerificationKind::Double:
      out << "double";
      break;
    case VerificationKind::Null:
      out << "null";
      break;
    case VerificationKind::UninitializedThis:
      out << "uninitialized-this";
      break;
    case VerificationKind::Object:
      out << type.reference;
      break;
    case VerificationKind::Uninitialized:
      out << "uninitialized " << type.reference << " at o" << type.allocation.index;
      break;
    case VerificationKind::WideContinuation:
      out << "wide-continuation";
      break;
  }
  out << " = ";
}

/**
 * @brief Render operands carried by an instruction itself. Control destinations appear on edges.
 * @param out Destination stream.
 * @param op Instruction or provenance anchor.
 */
void write_operands(std::ostream& out, const SsaOperation& op) {
  if (op.symbolic) {
    std::visit([&](const auto& operand) {
      if constexpr (requires { operand.owner; })
        out << ' ' << operand.owner << '.' << operand.name << ' ' << operand.descriptor;
      else if constexpr (requires { operand.bootstrap; })
        out << ' ' << operand.name << ' ' << operand.descriptor << " bootstrap "
            << operand.bootstrap.handle.owner << '.' << operand.bootstrap.handle.name << ' ' << operand.bootstrap.handle.descriptor;
    },
               *op.symbolic);
    return;
  }
  const auto& instruction = op.instruction;
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

/**
 * @brief Render the operations of one block, including provenance anchors and their local updates.
 * @param out Destination stream.
 * @param graph Method containing operation and value definitions.
 * @param block Block to emit in execution order.
 */
void write_block(std::ostream& out, const SsaMethod& graph, const SsaBlock& block) {
  out << "b" << block.id.index << ":\n";
  for (const auto id : block.phis) {
    const auto& phi = *graph.value(id);
    out << "  ";
    write_definition(out, phi);
    out << "phi";
    std::vector<SsaPhiInput> inputs(phi.incoming.begin(), phi.incoming.end());
    std::ranges::sort(inputs, {}, [](SsaPhiInput input) { return input.edge.index; });
    for (const auto input : inputs)
      out << " [e" << input.edge.index << ": v" << input.value.index << ']';
    out << '\n';
  }
  for (const auto id : block.operations) {
    const auto& op = *graph.operation(id);
    out << "  o" << id.index << ": ";
    if (op.result)
      write_definition(out, *graph.value(op.result));
    if (op.anchor)
      out << "anchor ";
    write_opcode(out, op.opcode);
    write_operands(out, op);
    for (const auto input : op.inputs)
      out << " v" << input.index;
    if (op.local_assignment)
      out << "  local[" << op.local_assignment->slot << "] <- v" << op.local_assignment->value.index;
    out << '\n';
  }
}

/**
 * @brief Print an incoming transfer, identifying its exact throwing site or switch arm.
 * @param out Destination stream.
 * @param edge Transfer to render.
 */
void write_edge(std::ostream& out, const SsaEdge& edge) {
  out << "e" << edge.id.index << ' ';
  switch (edge.kind) {
    case SsaEdgeKind::Entry:
      out << "entry";
      break;
    case SsaEdgeKind::DeadRoot:
      out << "dead-root";
      break;
    case SsaEdgeKind::Fallthrough:
      out << "fallthrough";
      break;
    case SsaEdgeKind::Conditional:
      out << "conditional";
      break;
    case SsaEdgeKind::Unconditional:
      out << "unconditional";
      break;
    case SsaEdgeKind::SwitchCase:
      out << "case";
      break;
    case SsaEdgeKind::SwitchDefault:
      out << "default";
      break;
    case SsaEdgeKind::Exceptional:
      out << "exceptional";
      break;
  }
  if (edge.case_key)
    out << ' ' << *edge.case_key;
  if (edge.constructor_uninitialized)
    out << " uninitialized-this";
  if (edge.source)
    out << " b" << edge.source.index;
  out << " -> b" << edge.target.index;
  if (edge.site)
    out << " at o" << edge.site.index;
  if (edge.handler)
    out << " handler h" << *edge.handler;
  if (edge.exception)
    out << " exception v" << edge.exception.index;
  out << '\n';
}
} // namespace

std::string dump_ssa(const SsaMethod& graph) {
  if (!graph.entry_block())
    return "empty SSA graph\n";
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "ssa method #" << graph.method_index() << '\n';
  const auto& storage = SsaAccess::get(graph);
  for (const auto& value : storage.values) {
    if (!value.id || !value.edge)
      continue;
    write_definition(out, value);
    switch (value.kind) {
      case SsaValueKind::Parameter:
        out << "parameter local[" << value.slot << ']';
        break;
      case SsaValueKind::RootLocal:
        out << "root local[" << value.slot << ']';
        break;
      case SsaValueKind::RootStack:
        out << "root stack[" << value.slot << ']';
        break;
      case SsaValueKind::Exception:
        out << "caught";
        break;
      default:
        break;
    }
    out << " on e" << value.edge.index << '\n';
  }
  for (const auto& block : graph.blocks())
    write_block(out, graph, block);
  for (const auto& edge : storage.edges)
    if (edge.id)
      write_edge(out, edge);
  for (std::size_t i = 0; i < graph.handlers().size(); ++i) {
    const auto& handler = graph.handlers()[i];
    out << 'h' << i << " catch ";
    if (handler.catch_type)
      out << '#' << handler.catch_type.value;
    else
      out << "all";
    out << " -> b" << handler.target.index << '\n';
  }
  return std::move(out).str();
}
} // namespace cafe::analysis
