#pragma once

#include <tuple>

#include "constant_pool.hpp"
#include "value.hpp"
#include "data_reader.hpp"
#include "class_file.hpp"

namespace cafe {

class CAFE_API class_reader {
public:
  class_reader() = default;
  ~class_reader() = default;
  class_reader(const class_reader&) = delete;
  class_reader& operator=(const class_reader&) = delete;
  class_reader(class_reader&&) noexcept = default;
  class_reader& operator=(class_reader&&) noexcept = default;

  result<class_file> read(data_reader&& reader);
  result<void> read(data_reader&& reader, class_file& file);

private:
  data_reader reader_{{}};
  cp::constant_pool pool_;
  std::vector<size_t> bootstrap_methods_;
  uint32_t class_version_{};
  size_t label_count_ = 0;

  result<void> read_code(code& code, uint32_t code_length);
  result<void> read_method(class_file& file);
  result<void> read_field(class_file& file);
  result<void> read_record(class_file& file);
  result<void> read_header();
  result<annotation> read_annotation();
  result<type_annotation> read_type_annotation(std::vector<std::pair<size_t, label>>& labels);
  result<element_value> read_element_value();
  result<frame_var> read_frame_var(std::vector<std::pair<size_t, label>>& labels);
  label get_label(std::vector<std::pair<size_t, label>>& labels, size_t offset);
  std::optional<label> get_label_opt(std::vector<std::pair<size_t, label>>& labels, size_t offset);

  result<std::string> get_string(uint16_t index);
  result<std::pair<std::string, std::string>> get_name_and_type(uint16_t index);
  result<std::tuple<std::string, std::string, std::string, bool>> get_ref(uint16_t index);
  result<method_handle> get_method_handle(uint16_t index);
  result<value> get_constant(uint16_t index);
  result<int32_t> get_int(uint16_t index) const;
  result<int64_t> get_long(uint16_t index) const;
  result<float> get_float(uint16_t index) const;
  result<double> get_double(uint16_t index) const;
  result<std::string> get_string(result<uint16_t> index);
  result<std::pair<std::string, std::string>> get_name_and_type(result<uint16_t> index);
  result<std::tuple<std::string, std::string, std::string, bool>> get_ref(result<uint16_t> index);
  result<method_handle> get_method_handle(result<uint16_t> index);
  result<value> get_constant(result<uint16_t> index);
  result<int32_t> get_int(result<uint16_t> index) const;
  result<int64_t> get_long(result<uint16_t> index) const;
  result<float> get_float(result<uint16_t> index) const;
  result<double> get_double(result<uint16_t> index) const;
};

} // namespace cafe
