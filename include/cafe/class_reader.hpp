#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "annotation.hpp"
#include "apidef.hpp"
#include "constant_pool.hpp"
#include "instruction.hpp"
#include "value.hpp"

namespace cafe {

class CAFE_API record_component_visitor {
public:
  virtual ~record_component_visitor() = default;
  virtual void visit(const std::string_view& name, const std::string_view& descriptor) = 0;
  virtual void visit_signature(const std::string_view& signature) = 0;
  virtual void visit_visible_annotation(const annotation& annotation) = 0;
  virtual void visit_invisible_annotation(const annotation& annotation) = 0;
  virtual void visit_visible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_invisible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API module_visitor {
public:
  virtual ~module_visitor() = default;
  virtual void visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) = 0;
  virtual void visit_require(const std::string_view& module, uint16_t access_flags,
                             const std::string_view& version) = 0;
  virtual void visit_export(const std::string_view& package, uint16_t access_flags,
                            const std::vector<std::string>& modules) = 0;
  virtual void visit_open(const std::string_view& package, uint16_t access_flags,
                          const std::vector<std::string>& modules) = 0;
  virtual void visit_use(const std::string_view& service) = 0;
  virtual void visit_provide(const std::string_view& service, const std::vector<std::string>& providers) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API code_visitor {
public:
  virtual ~code_visitor() = default;
  virtual void visit(uint16_t max_stack, uint16_t max_locals) = 0;
  virtual void visit_line_number(uint16_t line, label start) = 0;
  virtual void visit_tcb(const tcb& try_catch) = 0;
  virtual void visit_local(const local_var& local) = 0;
  virtual void visit_frame(const label& target, const frame& frame) = 0;
  virtual void visit_visible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_invisible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_label(const label& label) = 0;
  virtual void visit_insn(uint8_t opcode) = 0;
  virtual void visit_var_insn(uint8_t opcode, uint16_t index) = 0;
  virtual void visit_type_insn(uint8_t opcode, const std::string_view& type) = 0;
  virtual void visit_ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                              const std::string_view& descriptor) = 0;
  virtual void visit_iinc_insn(uint16_t index, int16_t value) = 0;
  virtual void visit_push_insn(value val) = 0;
  virtual void visit_branch_insn(uint8_t opcode, label target) = 0;
  virtual void visit_lookup_switch_insn(label default_target,
                                        const std::vector<std::pair<int32_t, label>>& targets) = 0;
  virtual void visit_table_switch_insn(label default_target, int32_t low, int32_t high,
                                       const std::vector<label>& targets) = 0;
  virtual void visit_multi_array_insn(const std::string_view& descriptor, uint8_t dims) = 0;
  virtual void visit_array_insn(const std::variant<uint8_t, std::string>& type) = 0;
  virtual void visit_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                         method_handle handle, const std::vector<value>& args) = 0;
  virtual void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API field_visitor {
public:
  virtual ~field_visitor() = default;
  virtual void visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) = 0;
  virtual void visit_constant_value(const value& constant_value) = 0;
  virtual void visit_synthetic(bool synthetic) = 0;
  virtual void visit_deprecated(bool deprecated) = 0;
  virtual void visit_signature(const std::string_view& signature) = 0;
  virtual void visit_visible_annotation(const annotation& annotation) = 0;
  virtual void visit_invisible_annotation(const annotation& annotation) = 0;
  virtual void visit_visible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_invisible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API method_visitor {
public:
  virtual ~method_visitor() = default;
  virtual void visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) = 0;
  virtual code_visitor& visit_code(uint16_t max_stack, uint16_t max_locals) = 0;
  virtual void visit_exception(const std::string_view& exception) = 0;
  virtual void visit_visible_parameter_annotation(uint8_t parameter, const annotation& annotation) = 0;
  virtual void visit_invisible_parameter_annotation(uint8_t parameter, const annotation& annotation) = 0;
  virtual void visit_annotation_default(const element_value& value) = 0;
  virtual void visit_parameter(uint16_t access_flags, const std::string_view& name) = 0;
  virtual void visit_synthetic(bool synthetic) = 0;
  virtual void visit_deprecated(bool deprecated) = 0;
  virtual void visit_signature(const std::string_view& signature) = 0;
  virtual void visit_visible_annotation(const annotation& annotation) = 0;
  virtual void visit_invisible_annotation(const annotation& annotation) = 0;
  virtual void visit_visible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_invisible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API class_visitor {
public:
  virtual ~class_visitor() = default;
  virtual void visit(uint32_t version, uint16_t access_flags, const std::string_view& name,
                     const std::string_view& super_name) = 0;
  virtual void visit_interface(const std::string_view& name) = 0;
  virtual field_visitor& visit_field(uint16_t access_flags, const std::string_view& name,
                                     const std::string_view& descriptor) = 0;
  virtual method_visitor& visit_method(uint16_t access_flags, const std::string_view& name,
                                       const std::string_view& descriptor) = 0;
  virtual void visit_source_file(const std::string_view& source_file) = 0;
  virtual void visit_inner_class(const std::string_view& name, const std::string_view& outer_name,
                                 const std::string_view& inner_name, uint16_t access_flags) = 0;
  virtual void visit_enclosing_method(const std::string_view& owner, const std::string_view& name,
                                      const std::string_view& descriptor) = 0;
  virtual void visit_source_debug_extension(const std::string_view& debug_extension) = 0;
  virtual module_visitor& visit_module(const std::string_view& name, uint16_t access_flags,
                                       const std::string_view& version) = 0;
  virtual void visit_module_package(const std::string_view& package) = 0;
  virtual void visit_module_main_class(const std::string_view& main_class) = 0;
  virtual void visit_nest_host(const std::string_view& host) = 0;
  virtual void visit_nest_member(const std::string_view& members) = 0;
  virtual record_component_visitor& visit_record_component(const std::string_view& name,
                                                           const std::string_view& descriptor) = 0;
  virtual void visit_permitted_subclass(const std::string_view& subclass) = 0;
  virtual void visit_synthetic(bool synthetic) = 0;
  virtual void visit_deprecated(bool deprecated) = 0;
  virtual void visit_signature(const std::string_view& signature) = 0;
  virtual void visit_visible_annotation(const annotation& annotation) = 0;
  virtual void visit_invisible_annotation(const annotation& annotation) = 0;
  virtual void visit_visible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_invisible_type_annotation(const type_annotation& annotation) = 0;
  virtual void visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) = 0;
  virtual void visit_end() = 0;
};

class CAFE_API class_reader {
public:
  explicit class_reader(std::istream& stream);
  explicit class_reader(const std::vector<int8_t>& buffer);
  ~class_reader() = default;
  class_reader(const class_reader&) = delete;
  class_reader& operator=(const class_reader&) = delete;
  class_reader(class_reader&&) noexcept = default;
  class_reader& operator=(class_reader&&) noexcept = default;

  void accept(class_visitor& visitor);

private:
  std::vector<int8_t> buffer_;
  std::vector<int8_t> byte_cache_;
  cp::constant_pool pool_;
  std::vector<size_t> bootstrap_methods_;
  uint32_t class_version_{};
  size_t cursor_ = 0;
  size_t label_count_ = 0;

  void read_code(code_visitor& visitor, uint32_t code_length);
  void read_method(class_visitor& visitor);
  void read_field(class_visitor& visitor);
  void read_record(class_visitor& visitor);
  void read_header();
  annotation read_annotation();
  type_annotation read_type_annotation(std::vector<std::pair<size_t, label>>& labels);
  element_value read_element_value();
  frame_var read_frame_var(std::vector<std::pair<size_t, label>>& labels);
  label get_label(std::vector<std::pair<size_t, label>>& labels, size_t offset);
  std::optional<label> get_label_opt(std::vector<std::pair<size_t, label>>& labels, size_t offset);
  std::string get_string(uint16_t index);
  std::pair<std::string, std::string> get_name_and_type(uint16_t index);
  std::tuple<std::string, std::string, std::string> get_ref(uint16_t index);
  method_handle get_method_handle(uint16_t index);
  value get_constant(uint16_t index);
  int32_t get_int(uint16_t index) const;
  int64_t get_long(uint16_t index) const;
  float get_float(uint16_t index) const;
  double get_double(uint16_t index) const;
  uint8_t read_u8();
  int8_t read_i8();
  uint16_t read_u16();
  int16_t read_i16();
  uint32_t read_u32();
  int32_t read_i32();
  uint64_t read_u64();
  int64_t read_i64();
  float read_f32();
  double read_f64();
  std::string read_utf();
};

} // namespace cafe
