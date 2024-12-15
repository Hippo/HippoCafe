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

  class_file read(data_reader&& reader);
  void read(data_reader&& reader, class_file& file);

private:
  data_reader reader_{{}};
  cp::constant_pool pool_;
  std::vector<size_t> bootstrap_methods_;
  uint32_t class_version_{};
  size_t label_count_ = 0;

  void read_code(code& code, uint32_t code_length);
  void read_method(class_file& file);
  void read_field(class_file& file);
  void read_record(class_file& file);
  void read_header();
  annotation read_annotation();
  type_annotation read_type_annotation(std::vector<std::pair<size_t, label>>& labels);
  element_value read_element_value();
  frame_var read_frame_var(std::vector<std::pair<size_t, label>>& labels);
  label get_label(std::vector<std::pair<size_t, label>>& labels, size_t offset);
  std::optional<label> get_label_opt(std::vector<std::pair<size_t, label>>& labels, size_t offset);
  std::string get_string(uint16_t index);
  std::pair<std::string, std::string> get_name_and_type(uint16_t index);
  std::tuple<std::string, std::string, std::string, bool> get_ref(uint16_t index);
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
