#pragma once

#include "class_reader.hpp"
#include "data_writer.hpp"
#include "class_tree.hpp"

namespace cafe {

inline constexpr uint8_t compute_maxes = 1;
inline constexpr uint8_t compute_frames = 2;

class CAFE_API class_writer {
public:
  class_writer() = default;
  explicit class_writer(uint8_t flags);
  class_writer(const class_tree& tree, uint8_t flags);
  ~class_writer() = default;
  class_writer(const class_writer&) = delete;
  class_writer(class_writer&&) = default;
  class_writer& operator=(const class_writer&) = delete;
  class_writer& operator=(class_writer&&) = delete;

  std::vector<int8_t> write(const class_file& file);

private:
  uint8_t flags_{};
  const class_tree* tree_{};
  cp::constant_pool pool_;
  std::vector<std::pair<uint16_t, std::vector<uint16_t>>> bsm_buffer_;
  std::vector<int8_t> attributes_;
  uint16_t attributes_count_ = 0;
  std::vector<int8_t> fields_;
  std::vector<int8_t> methods_;

  void write_source_file(const std::string_view& source_file);
  void write_enclosing_method(const std::string_view& owner, const std::optional<std::pair<std::string, std::string>>& method);
  void write_source_debug_extension(const std::string_view& debug_extension);
  void write_signature(const std::string_view& signature);
  void write_inner_classes(const std::vector<inner_class>& inner_classes);
  void write_module(const module& module);
  void write_module_packages(const std::vector<std::string>& module_packages);
  void write_module_main_class(const std::string_view& main_class);
  void write_nest_host(const std::string_view& host);
  void write_nest_members(const std::vector<std::string>& members);
  void write_record(const std::vector<record_component>& components);
  void write_permitted_subclasses(const std::vector<std::string>& subclasses);
  void write_annotations(const std::vector<annotation>& annotations, bool visible);
  void write_type_annotations(const std::vector<type_annotation>& annotations, bool visible);
  void write_fields(const std::vector<field>& fields);
  void write_methods(const class_file& file, const std::vector<method>& methods, bool oak);
  std::vector<int8_t> write_code(const class_file& file, const method& method, const code& code, bool oak);

  uint16_t get_class(const std::string_view& name);
  uint16_t get_field_ref(const std::string_view& owner, const std::string_view& name,
                         const std::string_view& desc);
  uint16_t get_method_ref(const std::string_view& owner, const std::string_view& name,
                          const std::string_view& desc);
  uint16_t get_interface_method_ref(const std::string_view& owner, const std::string_view& name,
                                    const std::string_view& desc);
  uint16_t get_string(const std::string_view& str);
  uint16_t get_int(int32_t value);
  uint16_t get_float(float value);
  uint16_t get_long(int64_t value);
  uint16_t get_double(double value);
  uint16_t get_name_and_type(const std::string_view& name, const std::string_view& desc);
  uint16_t get_utf(const std::string_view& utf);
  uint16_t get_method_handle(uint8_t reference_kind, const std::string_view& owner, const std::string_view& name,
                             const std::string_view& desc, bool interface);
  uint16_t get_method_type(const std::string_view& desc);
  uint16_t get_dynamic(const std::string_view& name, const std::string_view& desc, const method_handle& handle,
                       const std::vector<value>& args);
  uint16_t get_invoke_dynamic(const std::string_view& name, const std::string_view& desc,
                              const method_handle& handle, const std::vector<value>& args);
  uint16_t get_module(const std::string_view& name);
  uint16_t get_package(const std::string_view& name);
  uint16_t get_value(const value& val);
  uint16_t get_bsm(const method_handle& handle, const std::vector<value>& args);
  std::vector<int8_t> get_annotations(const std::vector<annotation>& annos);
  void get_element_value(data_writer& buf, const element_value& value);
  std::vector<int8_t> get_type_annotations(const std::vector<type_annotation>& annos,
                                          const std::vector<std::pair<size_t, label>>& labels);
};
} // namespace cafe
