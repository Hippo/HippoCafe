#pragma once

#include "class_reader.hpp"

namespace cafe {

class class_writer;

class CAFE_API record_component_writer : public record_component_visitor {
public:
  explicit record_component_writer(class_writer& writer);
  ~record_component_writer() override = default;
  record_component_writer(const record_component_writer&) = delete;
  record_component_writer(record_component_writer&&) = default;
  record_component_writer& operator=(const record_component_writer&) = delete;
  record_component_writer& operator=(record_component_writer&&) = delete;
  void visit(const std::string_view& name, const std::string_view& descriptor) override;
  void visit_signature(const std::string_view& signature) override;
  void visit_visible_annotation(const annotation& annotation) override;
  void visit_invisible_annotation(const annotation& annotation) override;
  void visit_visible_type_annotation(const type_annotation& annotation) override;
  void visit_invisible_type_annotation(const type_annotation& annotation) override;
  void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) override;
  void visit_end() override;

private:
  class_writer& writer_;
  uint16_t name_index_{};
  uint16_t descriptor_index_{};
  std::vector<int8_t> visible_annotations_;
  uint16_t visible_annotations_count_ = 0;
  std::vector<int8_t> invisible_annotations_;
  uint16_t invisible_annotations_count_ = 0;
  std::vector<int8_t> visible_type_annotations_;
  uint16_t visible_type_annotations_count_ = 0;
  std::vector<int8_t> invisible_type_annotations_;
  uint16_t invisible_type_annotations_count_ = 0;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
};

class CAFE_API module_writer : public module_visitor {
public:
  explicit module_writer(class_writer& writer);
  ~module_writer() override = default;
  module_writer(const module_writer&) = delete;
  module_writer(module_writer&&) = default;
  module_writer& operator=(const module_writer&) = delete;
  module_writer& operator=(module_writer&&) = delete;
  void visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) override;
  void visit_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version) override;
  void visit_export(const std::string_view& package, uint16_t access_flags,
                    const std::vector<std::string>& modules) override;
  void visit_open(const std::string_view& package, uint16_t access_flags,
                  const std::vector<std::string>& modules) override;
  void visit_use(const std::string_view& service) override;
  void visit_provide(const std::string_view& service, const std::vector<std::string>& providers) override;
  void visit_end() override;

private:
  class_writer& writer_;
  uint16_t name_index_{};
  uint16_t module_flags_{};
  uint16_t version_index_{};
  std::vector<int8_t> requires_;
  uint16_t requires_count_ = 0;
  std::vector<int8_t> exports_;
  uint16_t exports_count_ = 0;
  std::vector<int8_t> opens_;
  uint16_t opens_count_ = 0;
  std::vector<uint16_t> uses_;
  std::vector<int8_t> provides_;
  uint16_t provides_count_ = 0;
};

class method_writer;

class CAFE_API code_writer : public code_visitor {
public:
  code_writer(class_writer& writer, method_writer& parent);
  ~code_writer() override = default;
  code_writer(const code_writer&) = delete;
  code_writer(code_writer&&) = default;
  code_writer& operator=(const code_writer&) = delete;
  code_writer& operator=(code_writer&&) = delete;
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

private:
  class_writer& writer_;
  method_writer& parent_;
  uint16_t max_stack_{};
  uint16_t max_locals_{};
  std::vector<int8_t> line_numbers_;
  uint16_t line_numbers_count_ = 0;
  std::vector<int8_t> local_vars_;
  uint16_t local_vars_count_ = 0;
  std::vector<int8_t> local_type_vars_;
  uint16_t local_type_vars_count_ = 0;
  std::vector<int8_t> exception_table_;
  uint16_t exception_table_count_ = 0;
  std::vector<std::pair<size_t, label>> labels_;
  std::vector<std::pair<label, size_t>> estimates_;
  size_t estimated_size_ = 0;
  std::vector<int8_t> frames_;
  uint16_t frames_count_ = 0;
  std::vector<int8_t> visible_type_annotations_;
  uint16_t visible_type_annotations_count_ = 0;
  std::vector<int8_t> invisible_type_annotations_;
  uint16_t invisible_type_annotations_count_ = 0;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
  size_t last_label_ = 0;
  std::vector<uint8_t> code_;
  std::vector<size_t> switch_pads_;
  std::vector<std::tuple<label, size_t, bool, size_t>> branches_;
  std::vector<std::pair<uint16_t, label>> lines_;
  std::vector<tcb> tcbs_;
  std::vector<local_var> local_variables_;
  std::vector<std::pair<label, frame>> frames_stack_;
  std::vector<std::pair<type_annotation, bool>> type_annotations_;

  void finish_attributes();
  void shift(size_t start, size_t offset);
  size_t get_label(const label& lbl);
  size_t get_estimated(const label& lbl);
  std::vector<int8_t> write_frame_var(const frame_var& var);
  uint16_t next_delta(const label& target);
};

class CAFE_API field_writer : public field_visitor {
public:
  explicit field_writer(class_writer& writer);
  ~field_writer() override = default;
  field_writer(const field_writer&) = delete;
  field_writer(field_writer&&) = default;
  field_writer& operator=(const field_writer&) = delete;
  field_writer& operator=(field_writer&&) = delete;
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

private:
  class_writer& writer_;
  uint16_t name_index_{};
  uint16_t descriptor_index_{};
  uint16_t access_flags_{};
  std::vector<int8_t> visible_annotations_;
  uint16_t visible_annotations_count_ = 0;
  std::vector<int8_t> invisible_annotations_;
  uint16_t invisible_annotations_count_ = 0;
  std::vector<int8_t> visible_type_annotations_;
  uint16_t visible_type_annotations_count_ = 0;
  std::vector<int8_t> invisible_type_annotations_;
  uint16_t invisible_type_annotations_count_ = 0;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
};

class CAFE_API method_writer : public method_visitor {
public:
  explicit method_writer(class_writer& writer);
  ~method_writer() override = default;
  method_writer(const method_writer&) = delete;
  method_writer(method_writer&&) = default;
  method_writer& operator=(const method_writer&) = delete;
  method_writer& operator=(method_writer&&) = delete;
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

  friend class code_writer;

private:
  class_writer& writer_;
  uint16_t name_index_{};
  uint16_t descriptor_index_{};
  uint16_t access_flags_{};
  std::optional<code_writer> code_;
  std::vector<uint16_t> exceptions_;
  std::vector<std::vector<int8_t>> visible_parameter_annotations_;
  std::vector<uint16_t> visible_parameter_annotations_count_;
  std::vector<std::vector<int8_t>> invisible_parameter_annotations_;
  std::vector<uint16_t> invisible_parameter_annotations_count_;
  std::vector<int8_t> visible_annotations_;
  uint16_t visible_annotations_count_ = 0;
  std::vector<int8_t> invisible_annotations_;
  uint16_t invisible_annotations_count_ = 0;
  std::vector<int8_t> visible_type_annotations_;
  uint16_t visible_type_annotations_count_ = 0;
  std::vector<int8_t> invisible_type_annotations_;
  uint16_t invisible_type_annotations_count_ = 0;
  std::vector<int8_t> parameters_;
  uint8_t parameters_count_ = 0;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
};

class CAFE_API class_writer : public class_visitor {
public:
  class_writer();
  ~class_writer() override = default;
  class_writer(const class_writer&) = delete;
  class_writer(class_writer&&) = default;
  class_writer& operator=(const class_writer&) = delete;
  class_writer& operator=(class_writer&&) = delete;
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

  std::vector<int8_t> write() const;

  friend class field_writer;
  friend class method_writer;
  friend class module_writer;
  friend class code_writer;
  friend class record_component_writer;

private:
  cp::constant_pool pool_;
  std::vector<std::pair<uint16_t, std::vector<uint16_t>>> bsm_buffer_;
  std::vector<std::tuple<uint16_t, uint16_t, uint16_t, uint16_t>> inner_classes_;
  uint32_t version_{};
  uint16_t access_flags_{};
  uint16_t this_class_{};
  uint16_t super_class_{};
  std::vector<field_writer> fields_;
  std::vector<method_writer> methods_;
  std::optional<module_writer> module_;
  std::vector<record_component_writer> record_components_;
  std::vector<uint16_t> interfaces_;
  std::vector<uint16_t> module_packages_;
  std::vector<uint16_t> nest_members_;
  std::vector<uint16_t> permitted_subclasses_;
  std::vector<int8_t> visible_annotations_;
  uint16_t visible_annotations_count_ = 0;
  std::vector<int8_t> invisible_annotations_;
  uint16_t invisible_annotations_count_ = 0;
  std::vector<int8_t> visible_type_annotations_;
  uint16_t visible_type_annotations_count_ = 0;
  std::vector<int8_t> invisible_type_annotations_;
  uint16_t invisible_type_annotations_count_ = 0;
  std::vector<int8_t> record_components_bin_;
  uint16_t record_components_count_ = 0;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
  std::vector<int8_t> fields_bin_;
  uint16_t fields_count_ = 0;
  std::vector<int8_t> methods_bin_;
  uint16_t method_count_ = 0;

  uint16_t get_class(const std::string_view& name);
  uint16_t get_field_ref(const std::string_view& owner, const std::string_view& name,
                         const std::string_view& descriptor);
  uint16_t get_method_ref(const std::string_view& owner, const std::string_view& name,
                          const std::string_view& descriptor);
  uint16_t get_interface_method_ref(const std::string_view& owner, const std::string_view& name,
                                    const std::string_view& descriptor);
  uint16_t get_string(const std::string_view& str);
  uint16_t get_int(int32_t value);
  uint16_t get_float(float value);
  uint16_t get_long(int64_t value);
  uint16_t get_double(double value);
  uint16_t get_name_and_type(const std::string_view& name, const std::string_view& descriptor);
  uint16_t get_utf(const std::string_view& utf);
  uint16_t get_method_handle(uint8_t reference_kind, const std::string_view& owner, const std::string_view& name,
                             const std::string_view& descriptor);
  uint16_t get_method_type(const std::string_view& descriptor);
  uint16_t get_dynamic(const std::string_view& name, const std::string_view& descriptor, const method_handle& handle,
                       const std::vector<value>& args);
  uint16_t get_invoke_dynamic(const std::string_view& name, const std::string_view& descriptor,
                              const method_handle& handle, const std::vector<value>& args);
  uint16_t get_module(const std::string_view& name);
  uint16_t get_package(const std::string_view& name);
  uint16_t get_value(const value& val);
  uint16_t get_bsm(const method_handle& handle, const std::vector<value>& args);
  std::vector<int8_t> get_annotation(const annotation& anno);
  std::vector<int8_t> get_element_value(const element_value& value);
  std::vector<int8_t> get_type_annotation(const type_annotation& anno,
                                          const std::vector<std::pair<size_t, label>>& labels);
};
} // namespace cafe
