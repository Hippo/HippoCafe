#include "cafe/stub.hpp"

namespace cafe {

static stub_field_visitor g_field_visitor;
static stub_method_visitor g_method_visitor;
static stub_code_visitor g_code_visitor;
static stub_module_visitor g_module_visitor;
static stub_record_component_visitor g_record_component_visitor;

void stub_record_component_visitor::visit(const std::string_view& name, const std::string_view& descriptor) {
}
void stub_record_component_visitor::visit_signature(const std::string_view& signature) {
}
void stub_record_component_visitor::visit_visible_annotation(const annotation& annotation) {
}
void stub_record_component_visitor::visit_invisible_annotation(const annotation& annotation) {
}
void stub_record_component_visitor::visit_visible_type_annotation(const type_annotation& annotation) {
}
void stub_record_component_visitor::visit_invisible_type_annotation(const type_annotation& annotation) {
}
void stub_record_component_visitor::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
}
void stub_module_visitor::visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) {
}
void stub_module_visitor::visit_require(const std::string_view& module, uint16_t access_flags,
                                        const std::string_view& version) {
}
void stub_module_visitor::visit_export(const std::string_view& package, uint16_t access_flags,
                                       const std::vector<std::string>& modules) {
}
void stub_module_visitor::visit_open(const std::string_view& package, uint16_t access_flags,
                                     const std::vector<std::string>& modules) {
}
void stub_module_visitor::visit_use(const std::string_view& service) {
}
void stub_module_visitor::visit_provide(const std::string_view& service, const std::vector<std::string>& providers) {
}
void stub_code_visitor::visit(uint16_t max_stack, uint16_t max_locals) {
}
void stub_code_visitor::visit_line_number(uint16_t line, label start) {
}
void stub_code_visitor::visit_tcb(const tcb& try_catch) {
}
void stub_code_visitor::visit_local(const local_var& local) {
}
void stub_code_visitor::visit_frame(const label& target, const frame& frame) {
}
void stub_code_visitor::visit_visible_type_annotation(const type_annotation& annotation) {
}
void stub_code_visitor::visit_invisible_type_annotation(const type_annotation& annotation) {
}
void stub_code_visitor::visit_label(const label& label) {
}
void stub_code_visitor::visit_insn(uint8_t opcode) {
}
void stub_code_visitor::visit_var_insn(uint8_t opcode, uint16_t index) {
}
void stub_code_visitor::visit_type_insn(uint8_t opcode, const std::string_view& type) {
}
void stub_code_visitor::visit_ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                                       const std::string_view& descriptor) {
}
void stub_code_visitor::visit_iinc_insn(uint16_t index, int16_t value) {
}
void stub_code_visitor::visit_push_insn(value val) {
}
void stub_code_visitor::visit_branch_insn(uint8_t opcode, label target) {
}
void stub_code_visitor::visit_lookup_switch_insn(label default_target,
                                                 const std::vector<std::pair<int32_t, label>>& targets) {
}
void stub_code_visitor::visit_table_switch_insn(label default_target, int32_t low, int32_t high,
                                                const std::vector<label>& targets) {
}
void stub_code_visitor::visit_multi_array_insn(const std::string_view& descriptor, uint8_t dims) {
}
void stub_code_visitor::visit_array_insn(const std::variant<uint8_t, std::string>& type) {
}
void stub_code_visitor::visit_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                                  method_handle handle, const std::vector<value>& args) {
}
void stub_code_visitor::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
}
void stub_field_visitor::visit(uint16_t access_flags, const std::string_view& name,
                               const std::string_view& descriptor) {
}
void stub_field_visitor::visit_constant_value(const value& constant_value) {
}
void stub_field_visitor::visit_synthetic(bool synthetic) {
}
void stub_field_visitor::visit_deprecated(bool deprecated) {
}
void stub_field_visitor::visit_signature(const std::string_view& signature) {
}
void stub_field_visitor::visit_visible_annotation(const annotation& annotation) {
}
void stub_field_visitor::visit_invisible_annotation(const annotation& annotation) {
}
void stub_field_visitor::visit_visible_type_annotation(const type_annotation& annotation) {
}
void stub_field_visitor::visit_invisible_type_annotation(const type_annotation& annotation) {
}
void stub_field_visitor::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
}
void stub_method_visitor::visit(uint16_t access_flags, const std::string_view& name,
                                const std::string_view& descriptor) {
}
code_visitor& stub_method_visitor::visit_code(uint16_t max_stack, uint16_t max_locals) {
  return g_code_visitor;
}
void stub_method_visitor::visit_exception(const std::string_view& exception) {
}
void stub_method_visitor::visit_visible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
}
void stub_method_visitor::visit_invisible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
}
void stub_method_visitor::visit_annotation_default(const element_value& value) {
}
void stub_method_visitor::visit_parameter(uint16_t access_flags, const std::string_view& name) {
}
void stub_method_visitor::visit_synthetic(bool synthetic) {
}
void stub_method_visitor::visit_deprecated(bool deprecated) {
}
void stub_method_visitor::visit_signature(const std::string_view& signature) {
}
void stub_method_visitor::visit_visible_annotation(const annotation& annotation) {
}
void stub_method_visitor::visit_invisible_annotation(const annotation& annotation) {
}
void stub_method_visitor::visit_visible_type_annotation(const type_annotation& annotation) {
}
void stub_method_visitor::visit_invisible_type_annotation(const type_annotation& annotation) {
}
void stub_method_visitor::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
}
void stub_class_visitor::visit(uint32_t version, uint16_t access_flags, const std::string_view& name,
                               const std::string_view& super_name) {
}
void stub_class_visitor::visit_interface(const std::string_view& name) {
}
field_visitor& stub_class_visitor::visit_field(uint16_t access_flags, const std::string_view& name,
                                               const std::string_view& descriptor) {
  return g_field_visitor;
}
method_visitor& stub_class_visitor::visit_method(uint16_t access_flags, const std::string_view& name,
                                                 const std::string_view& descriptor) {
  return g_method_visitor;
}
void stub_class_visitor::visit_source_file(const std::string_view& source_file) {
}
void stub_class_visitor::visit_inner_class(const std::string_view& name, const std::string_view& outer_name,
                                           const std::string_view& inner_name, uint16_t access_flags) {
}
void stub_class_visitor::visit_enclosing_method(const std::string_view& owner, const std::string_view& name,
                                                const std::string_view& descriptor) {
}
void stub_class_visitor::visit_source_debug_extension(const std::string_view& debug_extension) {
}
module_visitor& stub_class_visitor::visit_module(const std::string_view& name, uint16_t access_flags,
                                                 const std::string_view& version) {
  return g_module_visitor;
}
void stub_class_visitor::visit_module_package(const std::string_view& package) {
}
void stub_class_visitor::visit_module_main_class(const std::string_view& main_class) {
}
void stub_class_visitor::visit_nest_host(const std::string_view& host) {
}
void stub_class_visitor::visit_nest_member(const std::string_view& members) {
}
record_component_visitor& stub_class_visitor::visit_record_component(const std::string_view& name,
                                                                     const std::string_view& descriptor) {
  return g_record_component_visitor;
}
void stub_class_visitor::visit_permitted_subclass(const std::string_view& subclass) {
}
void stub_class_visitor::visit_synthetic(bool synthetic) {
}
void stub_class_visitor::visit_deprecated(bool deprecated) {
}
void stub_class_visitor::visit_signature(const std::string_view& signature) {
}
void stub_class_visitor::visit_visible_annotation(const annotation& annotation) {
}
void stub_class_visitor::visit_invisible_annotation(const annotation& annotation) {
}
void stub_class_visitor::visit_visible_type_annotation(const type_annotation& annotation) {
}
void stub_class_visitor::visit_invisible_type_annotation(const type_annotation& annotation) {
}
void stub_class_visitor::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
}
void stub_record_component_visitor::visit_end() {
}
void stub_module_visitor::visit_end() {
}
void stub_code_visitor::visit_end() {
}
void stub_field_visitor::visit_end() {
}
void stub_method_visitor::visit_end() {
}
void stub_class_visitor::visit_end() {
}
} // namespace cafe
