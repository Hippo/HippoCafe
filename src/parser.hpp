#pragma once

#include <functional>

#include "cafe/class_file.hpp"
#include "cafe/data_rw.hpp"

namespace cafe {

class class_parser {
public:
  explicit class_parser(const std::function<void(const std::exception&)>& error_handler);
  cp::constant_pool parse_constant_pool(data_reader& reader);
  attribute::type_annotation parse_type_annotation(data_reader& reader);
  attribute::annotation parse_annotation(data_reader& reader);
  attribute::element_value parse_element_value(data_reader& reader);
  attribute::stack_map_frame parse_stack_map_frame(data_reader& reader);
  attribute::verification_type_info parse_verification_type_info(data_reader& reader);
  attribute::attribute parse_attribute(data_reader& reader, const cp::constant_pool& pool, bool oak);
  void parse(data_reader& reader, class_file& file);

private:
  std::function<void(const std::exception&)> error_handler_;
};

} // namespace cafe
