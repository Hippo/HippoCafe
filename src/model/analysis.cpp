#include "cafe/model/analysis.hpp"
#include "cafe/constants.hpp"

namespace cafe::analysis {

size_t max_size::operator()(const label&) const {
  return 0;
}
size_t max_size::operator()(const insn&) const {
  return 1;
}
size_t max_size::operator()(const var_insn& instr) const {
  return 1 + (instr.index > std::numeric_limits<uint8_t>::max() ? 2 : 1);
}
size_t max_size::operator()(const type_insn&) const {
  return 3;
}
size_t max_size::operator()(const ref_insn& instr) const {
  return instr.opcode == op::invokeinterface ? 5 : 3;
}
size_t max_size::operator()(const iinc_insn& instr) const {
  auto wide = instr.index > std::numeric_limits<uint8_t>::max() || instr.value > std::numeric_limits<int8_t>::max() ||
              instr.value < std::numeric_limits<int8_t>::min();
  return 3 + (wide ? 2 : 0);
}
size_t max_size::operator()(const push_insn& instr) const {
  auto opcode = instr.opcode();
  if (opcode >= op::iconst_m1 && opcode <= op::iconst_5) {
    return 1;
  }
  if (opcode == op::bipush) {
    return 2;
  }
  // assume ldc_w
  return 3;
}
size_t max_size::operator()(const branch_insn& instr) const {
  switch (instr.opcode) {
    // assume wide jumps
    case op::goto_:
    case op::jsr:
    // wide insns should not be used directly, but match them just in case
    case op::goto_w:
    case op::jsr_w:
      return 5;
    default:
      return 3;
  }
}
size_t max_size::operator()(const lookup_switch_insn& instr) const {
  // assume 3 byte pad
  return 12 + (instr.targets.size() * 8);
}
size_t max_size::operator()(const table_switch_insn& instr) const {
  return 16 + (instr.targets.size() * 4);
}
size_t max_size::operator()(const multi_array_insn&) const {
  return 4;
}
size_t max_size::operator()(const array_insn& instr) const {
  return std::holds_alternative<uint8_t>(instr.type) ? 2 : 3;
}
size_t max_size::operator()(const invoke_dynamic_insn&) const {
  return 5;
}

} // namespace cafe::analysis
