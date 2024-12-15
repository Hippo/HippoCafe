#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <istream>

namespace cafe {

class data_reader {
public:
  data_reader(std::vector<int8_t>&& data);
  data_reader(const std::vector<int8_t>& data);
  data_reader(std::istream& stream);
  ~data_reader() = default;
  data_reader(const data_reader&) = delete;
  data_reader(data_reader&&) = default;
  data_reader& operator=(const data_reader&) = delete;
  data_reader& operator=(data_reader&&) = default;

  int8_t read_i8();
  uint8_t read_u8();
  int16_t read_i16();
  uint16_t read_u16();
  int32_t read_i32();
  uint32_t read_u32();
  int64_t read_i64();
  uint64_t read_u64();
  float read_f32();
  double read_f64();
  std::string read_utf();

  const std::vector<int8_t>& data() const;
  std::vector<int8_t>& data();

  size_t cursor() const;
  void cursor(size_t cursor);
  void skip(size_t count);

private:
  std::vector<int8_t> buffer_;
  std::vector<uint8_t> byte_cache_;
  size_t cursor_ = 0;
};

}