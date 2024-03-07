#pragma once

#include "cafe/class_file.hpp"
#include "cafe/data_rw.hpp"

namespace cafe {
class constant_pool_visitor {
public:
  explicit constant_pool_visitor(data_writer& writer);

  void operator()(const cp::pad_info& info) const;

  void operator()(const cp::class_info& info);

  void operator()(const cp::field_ref_info& info);

  void operator()(const cp::method_ref_info& info);

  void operator()(const cp::interface_method_ref_info& info);

  void operator()(const cp::string_info& info);

  void operator()(const cp::integer_info& info);

  void operator()(const cp::float_info& info);

  void operator()(const cp::long_info& info);

  void operator()(const cp::double_info& info);

  void operator()(const cp::name_and_type_info& info);

  void operator()(const cp::utf8_info& info);

  void operator()(const cp::method_handle_info& info);

  void operator()(const cp::method_type_info& info);

  void operator()(const cp::dynamic_info& info);

  void operator()(const cp::invoke_dynamic_info& info);

  void operator()(const cp::module_info& info);

  void operator()(const cp::package_info& info);

private:
  data_writer& writer_;
};

class attribute_visitor {
public:
  attribute_visitor(data_writer& writer, const cp::constant_pool& pool, bool oak);

  void write_name(const std::string_view& name);

  void operator()(const attribute::unknown& attr);
  void operator()(const attribute::constant_value& attr);
  void operator()(const attribute::code& attr);
  void operator()(const attribute::stack_map_table& attr);
  void operator()(const attribute::exceptions& attr);
  void operator()(const attribute::inner_classes& attr);
  void operator()(const attribute::enclosing_method& attr);
  void operator()(const attribute::synthetic& attr);
  void operator()(const attribute::signature& attr);
  void operator()(const attribute::source_file& attr);
  void operator()(const attribute::source_debug_extension& attr);
  void operator()(const attribute::line_number_table& attr);
  void operator()(const attribute::local_variable_table& attr);
  void operator()(const attribute::local_variable_type_table& attr);
  void operator()(const attribute::deprecated& attr);
  void operator()(const attribute::runtime_visible_annotations& attr);
  void operator()(const attribute::runtime_invisible_annotations& attr);
  void operator()(const attribute::runtime_visible_parameter_annotations& attr);
  void operator()(const attribute::runtime_invisible_parameter_annotations& attr);
  void operator()(const attribute::runtime_visible_type_annotations& attr);
  void operator()(const attribute::runtime_invisible_type_annotations& attr);
  void operator()(const attribute::annotation_default& attr);
  void operator()(const attribute::bootstrap_methods& attr);
  void operator()(const attribute::method_parameters& attr);
  void operator()(const attribute::module& attr);
  void operator()(const attribute::module_packages& attr);
  void operator()(const attribute::module_main_class& attr);
  void operator()(const attribute::nest_host& attr);
  void operator()(const attribute::nest_members& attr);
  void operator()(const attribute::record& attr);
  void operator()(const attribute::permitted_subclasses& attr);

private:
  data_writer& writer_;
  const cp::constant_pool& pool_;
  bool oak_;
};
} // namespace cafe
