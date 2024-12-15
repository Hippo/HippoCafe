#include "visitor.hpp"

namespace cafe {
constant_pool_visitor::constant_pool_visitor(data_writer& writer) : writer_(writer) {
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
  writer_.write_u16(info.desc_index);
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
  writer_.write_u16(info.desc_index);
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
} // namespace cafe
