#pragma once

#include "class_reader.hpp"

namespace cafe {


class CAFE_API attribute {
public:
  std::string name;
  std::vector<int8_t> data;
  attribute() = default;
  attribute(const std::string_view& name, const std::vector<int8_t>& data);
  ~attribute() = default;
  attribute(const attribute& other) = default;
  attribute(attribute&& other) noexcept = default;
  attribute& operator=(const attribute& other) = default;
  attribute& operator=(attribute&& other) noexcept = default;
};

class CAFE_API record_component : public record_component_visitor {
public:
  std::string name;
  std::string descriptor;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  record_component() = default;
  record_component(const std::string_view& name, const std::string_view& descriptor);
  record_component(const record_component& other) = default;
  record_component(record_component&& other) noexcept = default;
  record_component& operator=(const record_component& other) = default;
  record_component& operator=(record_component&& other) noexcept = default;
  ~record_component() override = default;
  void visit(const std::string_view& name, const std::string_view& descriptor) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;
  void accept(record_component_visitor& visitor) const;
};

class CAFE_API module_require {
public:
  std::string module;
  uint16_t access_flags{};
  std::string version;
  module_require() = default;
  module_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version);
  ~module_require() = default;
  module_require(const module_require& other) = default;
  module_require(module_require&& other) noexcept = default;
  module_require& operator=(const module_require& other) = default;
  module_require& operator=(module_require&& other) noexcept = default;
};

class CAFE_API module_export {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  module_export() = default;
  module_export(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~module_export() = default;
  module_export(const module_export& other) = default;
  module_export(module_export&& other) noexcept = default;
  module_export& operator=(const module_export& other) = default;
  module_export& operator=(module_export&& other) noexcept = default;
};

class CAFE_API module_open {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  module_open() = default;
  module_open(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~module_open() = default;
  module_open(const module_open& other) = default;
  module_open(module_open&& other) noexcept = default;
  module_open& operator=(const module_open& other) = default;
  module_open& operator=(module_open&& other) noexcept = default;
};

class CAFE_API module_provide {
public:
  std::string service;
  std::vector<std::string> providers;
  module_provide() = default;
  module_provide(const std::string_view& service, const std::vector<std::string>& providers);
  ~module_provide() = default;
  module_provide(const module_provide& other) = default;
  module_provide(module_provide&& other) noexcept = default;
  module_provide& operator=(const module_provide& other) = default;
  module_provide& operator=(module_provide&& other) noexcept = default;
};

class CAFE_API module : public module_visitor {
public:
  std::string name;
  uint16_t access_flags{};
  std::string version;
  std::vector<module_require>
    requires;
  std::vector<module_export> exports;
  std::vector<module_open> opens;
  std::vector<std::string> uses;
  std::vector<module_provide> provides;
  module() = default;
  module(const std::string_view& name, uint16_t access_flags, const std::string_view& version);
  ~module() override = default;
  module(const module& other) = default;
  module(module&& other) noexcept = default;
  module& operator=(const module& other) = default;
  module& operator=(module&& other) noexcept = default;
  void visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) override;
  void visit_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version) override;
  void visit_export(const std::string_view& package, uint16_t access_flags,
                    const std::vector<std::string>& modules) override;
  void visit_open(const std::string_view& package, uint16_t access_flags,
                  const std::vector<std::string>& modules) override;
  void visit_use(const std::string_view& service) override;
  void visit_provide(const std::string_view& service, const std::vector<std::string>& providers) override;
  void visit_end() override;
  void accept(module_visitor& visitor) const;
};

class CAFE_API code : public code_visitor, public std::vector<instruction> {
public:
  uint16_t max_stack{};
  uint16_t max_locals{};
  std::vector<std::pair<uint16_t, label>> line_numbers;
  std::vector<tcb> tcbs;
  std::vector<local_var> locals;
  std::vector<std::pair<label, frame>> frames;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
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
  void visit_push_insn(value value) override;
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
  void accept(code_visitor& visitor) const;
};

class CAFE_API field : public field_visitor {
public:
  uint16_t access_flags{};
  std::string name;
  std::string descriptor;
  std::optional<value> constant_value;
  bool synthetic = false;
  bool deprecated = false;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  field() = default;
  field(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor);
  ~field() override = default;
  field(const field& other) = default;
  field(field&& other) noexcept = default;
  field& operator=(const field& other) = default;
  field& operator=(field&& other) noexcept = default;
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
  void accept(field_visitor& visitor) const;
};

class CAFE_API method : public method_visitor {
public:
  uint16_t access_flags{};
  std::string name;
  std::string descriptor;
  code code;
  std::vector<std::string> exceptions;
  std::vector<std::vector<annotation>> visible_parameter_annotations;
  std::vector<std::vector<annotation>> invisible_parameter_annotations;
  std::optional<element_value> annotation_default;
  std::vector<std::pair<uint16_t, std::string>> parameters;
  bool synthetic = false;
  bool deprecated = false;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  method() = default;
  method(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor);
  ~method() override = default;
  method(const method& other) = default;
  method(method&& other) noexcept = default;
  method& operator=(const method& other) = default;
  method& operator=(method&& other) noexcept = default;
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
  void accept(method_visitor& visitor) const;
};

class CAFE_API inner_class {
public:
  std::string name;
  std::string outer_name;
  std::string inner_name;
  uint16_t access_flags{};
  inner_class() = default;
  inner_class(const std::string_view& name, const std::string_view& outer_name, const std::string_view& inner_name,
              uint16_t access_flags);
  ~inner_class() = default;
  inner_class(const inner_class& other) = default;
  inner_class(inner_class&& other) noexcept = default;
  inner_class& operator=(const inner_class& other) = default;
  inner_class& operator=(inner_class&& other) noexcept = default;
};

class CAFE_API class_file : public class_visitor {
public:
  uint32_t version{};
  uint16_t access_flags{};
  std::string name;
  std::string super_name;
  std::vector<std::string> interfaces;
  std::vector<field> fields;
  std::vector<method> methods;
  std::string source_file;
  std::vector<inner_class> inner_classes;
  std::string enclosing_method_owner;
  std::string enclosing_method_name;
  std::string enclosing_method_descriptor;
  std::string source_debug_extension;
  std::optional<module> module;
  std::vector<std::string> module_packages;
  std::string module_main_class;
  std::string nest_host;
  std::vector<std::string> nest_members;
  std::vector<record_component> record_components;
  std::vector<std::string> permitted_subclasses;
  bool synthetic = false;
  bool deprecated = false;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  class_file() = default;
  class_file(uint32_t version, uint16_t access_flags, const std::string_view& name, const std::string_view& super_name);
  ~class_file() override = default;
  class_file(const class_file& other) = default;
  class_file(class_file&& other) noexcept = default;
  class_file& operator=(const class_file& other) = default;
  class_file& operator=(class_file&& other) noexcept = default;
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

  void accept(class_visitor& visitor) const;
};

CAFE_API std::istream& operator>>(std::istream& is, class_file& cf);
CAFE_API std::ostream& operator<<(std::ostream& os, const class_file& cf);
} // namespace cafe
