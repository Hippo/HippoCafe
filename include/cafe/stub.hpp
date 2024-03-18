#pragma once

#include "class_reader.hpp"

namespace cafe {

class CAFE_API stub_record_component_visitor : public record_component_visitor {
public:
  void visit(const std::string_view& name, const std::string_view& descriptor) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
};

class CAFE_API stub_module_visitor : public module_visitor {
public:
  void visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) override;
  void visit_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version) override;
  void visit_export(const std::string_view& package, uint16_t access_flags,
                    const std::vector<std::string>& modules) override;
  void visit_open(const std::string_view& package, uint16_t access_flags,
                  const std::vector<std::string>& modules) override;
  void visit_use(const std::string_view& service) override;
  void visit_provide(const std::string_view& service, const std::vector<std::string>& providers) override;
  void visit_end() override;
};

class CAFE_API stub_code_visitor : public code_visitor {
public:
  void visit(uint16_t max_stack, uint16_t max_locals) override;
  void visit_line_number(uint16_t line, label start) override;
  void visit_tcb(const tcb& try_catch) override;
  void visit_local(const local_var& local) override;
  void visit_frame(const label& target, const frame& frame) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_label(const label& label) override;
  void visit_insn(uint8_t opcode) override;
  void visit_var_insn(uint8_t opcode, uint16_t index) override;
  void visit_type_insn(uint8_t opcode, const std::string_view& type) override;
  void visit_ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                      const std::string_view& descriptor) override;
  void visit_iinc_insn(uint16_t index, int16_t value) override;
  void visit_push_insn(value val) override;
  void visit_branch_insn(uint8_t opcode, label target) override;
  void visit_lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets) override;
  void visit_table_switch_insn(label default_target, int32_t low, int32_t high,
                               const std::vector<label>& targets) override;
  void visit_multi_array_insn(const std::string_view& descriptor, uint8_t dims) override;
  void visit_array_insn(const std::variant<uint8_t, std::string>& type) override;
  void visit_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor, method_handle handle,
                                 const std::vector<value>& args) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
};

class CAFE_API stub_field_visitor : public field_visitor {
public:
  void visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) override;
  void visit_constant_value(const value& constant_value) override;
  void visit_synthetic(bool synthetic) override;
  void visit_deprecated(bool deprecated) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
};

class CAFE_API stub_method_visitor : public method_visitor {
public:
  void visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) override;
  code_visitor& visit_code(uint16_t max_stack, uint16_t max_locals) override;
  void visit_exception(const std::string_view& exception) override;
  void visit_visible_parameter_annotation(uint8_t parameter, const annotation& annotation) override;
  void visit_invisible_parameter_annotation(uint8_t parameter, const annotation& annotation) override;
  void visit_annotation_default(const element_value& value) override;
  void visit_parameter(uint16_t access_flags, const std::string_view& name) override;
  void visit_synthetic(bool synthetic) override;
  void visit_deprecated(bool deprecated) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
};

class CAFE_API stub_class_visitor : public class_visitor {
public:
  void visit(uint32_t version, uint16_t access_flags, const std::string_view& name,
             const std::string_view& super_name) override;
  void visit_interface(const std::string_view& name) override;
  field_visitor& visit_field(uint16_t access_flags, const std::string_view& name,
                             const std::string_view& descriptor) override;
  method_visitor& visit_method(uint16_t access_flags, const std::string_view& name,
                               const std::string_view& descriptor) override;
  void visit_source_file(const std::string_view& source_file) override;
  void visit_inner_class(const std::string_view& name, const std::string_view& outer_name,
                         const std::string_view& inner_name, uint16_t access_flags) override;
  void visit_enclosing_method(const std::string_view& owner, const std::string_view& name,
                              const std::string_view& descriptor) override;
  void visit_source_debug_extension(const std::string_view& debug_extension) override;
  module_visitor& visit_module(const std::string_view& name, uint16_t access_flags,
                               const std::string_view& version) override;
  void visit_module_package(const std::string_view& package) override;
  void visit_module_main_class(const std::string_view& main_class) override;
  void visit_nest_host(const std::string_view& host) override;
  void visit_nest_member(const std::string_view& members) override;
  record_component_visitor& visit_record_component(const std::string_view& name,
                                                   const std::string_view& descriptor) override;
  void visit_permitted_subclass(const std::string_view& subclass) override;
  void visit_synthetic(bool synthetic) override;
  void visit_deprecated(bool deprecated) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
};

} // namespace cafe
