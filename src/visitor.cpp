#include "visitor.hpp"

namespace cafe {
constant_pool_visitor::constant_pool_visitor(bytebuf& writer) : writer_(writer) {
}
void constant_pool_visitor::operator()(const cp::pad_info&) const {
}

void constant_pool_visitor::operator()(const cp::class_info& info) {
  writer_.write_u8(cp::class_info::tag);
  writer_.write_u16(info.name_index);
}

void constant_pool_visitor::operator()(const cp::field_ref_info& info) {
  writer_.write_u8(cp::field_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::method_ref_info& info) {
  writer_.write_u8(cp::method_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::interface_method_ref_info& info) {
  writer_.write_u8(cp::interface_method_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::string_info& info) {
  writer_.write_u8(cp::string_info::tag);
  writer_.write_u16(info.string_index);
}

void constant_pool_visitor::operator()(const cp::integer_info& info) {
  writer_.write_u8(cp::integer_info::tag);
  writer_.write_u32(static_cast<uint32_t>(info.value));
}

void constant_pool_visitor::operator()(const cp::float_info& info) {
  writer_.write_u8(cp::float_info::tag);
  writer_.write_f32(info.value);
}

void constant_pool_visitor::operator()(const cp::long_info& info) {
  writer_.write_u8(cp::long_info::tag);
  writer_.write_u64(static_cast<uint64_t>(info.value));
}

void constant_pool_visitor::operator()(const cp::double_info& info) {
  writer_.write_u8(cp::double_info::tag);
  writer_.write_f64(info.value);
}

void constant_pool_visitor::operator()(const cp::name_and_type_info& info) {
  writer_.write_u8(cp::name_and_type_info::tag);
  writer_.write_u16(info.name_index);
  writer_.write_u16(info.descriptor_index);
}

void constant_pool_visitor::operator()(const cp::utf8_info& info) {
  writer_.write_u8(cp::utf8_info::tag);
  writer_.write_utf(info.value);
}

void constant_pool_visitor::operator()(const cp::method_handle_info& info) {
  writer_.write_u8(cp::method_handle_info::tag);
  writer_.write_u8(info.reference_kind);
  writer_.write_u16(info.reference_index);
}

void constant_pool_visitor::operator()(const cp::method_type_info& info) {
  writer_.write_u8(cp::method_type_info::tag);
  writer_.write_u16(info.descriptor_index);
}

void constant_pool_visitor::operator()(const cp::dynamic_info& info) {
  writer_.write_u8(cp::dynamic_info::tag);
  writer_.write_u16(info.bootstrap_method_attr_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::invoke_dynamic_info& info) {
  writer_.write_u8(cp::invoke_dynamic_info::tag);
  writer_.write_u16(info.bootstrap_method_attr_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::module_info& info) {
  writer_.write_u8(cp::module_info::tag);
  writer_.write_u16(info.name_index);
}

void constant_pool_visitor::operator()(const cp::package_info& info) {
  writer_.write_u8(cp::package_info::tag);
  writer_.write_u16(info.name_index);
}
instruction_visitor::instruction_visitor(code_visitor& visitor) : visitor_(visitor) {
}
void instruction_visitor::operator()(const label& label) {
  visitor_.visit_label(label);
}
void instruction_visitor::operator()(const insn& insn) {
  visitor_.visit_insn(insn.opcode);
}
void instruction_visitor::operator()(const var_insn& insn) {
  visitor_.visit_var_insn(insn.opcode, insn.index);
}
void instruction_visitor::operator()(const type_insn& insn) {
  visitor_.visit_type_insn(insn.opcode, insn.type);
}
void instruction_visitor::operator()(const ref_insn& insn) {
  visitor_.visit_ref_insn(insn.opcode, insn.owner, insn.name, insn.descriptor);
}
void instruction_visitor::operator()(const iinc_insn& insn) {
  visitor_.visit_iinc_insn(insn.index, insn.value);
}
void instruction_visitor::operator()(const push_insn& insn) {
  visitor_.visit_push_insn(insn.value);
}
void instruction_visitor::operator()(const branch_insn& insn) {
  visitor_.visit_branch_insn(insn.opcode, insn.target);
}
void instruction_visitor::operator()(const lookup_switch_insn& insn) {
  visitor_.visit_lookup_switch_insn(insn.default_target, insn.targets);
}
void instruction_visitor::operator()(const table_switch_insn& insn) {
  visitor_.visit_table_switch_insn(insn.default_target, insn.low, insn.high, insn.targets);
}
void instruction_visitor::operator()(const multi_array_insn& insn) {
  visitor_.visit_multi_array_insn(insn.descriptor, insn.dims);
}
void instruction_visitor::operator()(const array_insn& insn) {
  visitor_.visit_array_insn(insn.type);
}
void instruction_visitor::operator()(const invoke_dynamic_insn& insn) {
  visitor_.visit_invoke_dynamic_insn(insn.name, insn.descriptor, insn.handle, insn.args);
}
} // namespace cafe
