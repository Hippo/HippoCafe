#pragma once

#include "../include/cafe/data_writer.hpp"
#include "cafe/class_reader.hpp"
#include "cafe/constant_pool.hpp"

namespace cafe {
class constant_pool_visitor {
public:
  explicit constant_pool_visitor(data_writer& writer);
  void operator()(const cp::pad_info&) const;

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

} // namespace cafe
