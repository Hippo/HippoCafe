#pragma once

#include "instruction.hpp"

namespace cafe::analysis {
class max_size {
public:
  max_size() = default;
  size_t operator()(const label&) const;
  size_t operator()(const insn&) const;
  size_t operator()(const var_insn& instr) const;
  size_t operator()(const type_insn&) const;
  size_t operator()(const ref_insn& instr) const;
  size_t operator()(const iinc_insn& instr) const;
  size_t operator()(const push_insn& instr) const;
  size_t operator()(const branch_insn& instr) const;
  size_t operator()(const lookup_switch_insn& instr) const;
  size_t operator()(const table_switch_insn& instr) const;
  size_t operator()(const multi_array_insn&) const;
  size_t operator()(const array_insn& instr) const;
  size_t operator()(const invoke_dynamic_insn&) const;
};
} // namespace cafe::analysis
