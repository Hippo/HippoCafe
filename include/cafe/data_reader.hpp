#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <istream>

#include "result.hpp"

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

  result<int8_t> read_i8();
  result<uint8_t> read_u8();
  result<int16_t> read_i16();
  result<uint16_t> read_u16();
  result<int32_t> read_i32();
  result<uint32_t> read_u32();
  result<int64_t> read_i64();
  result<uint64_t> read_u64();
  result<float> read_f32();
  result<double> read_f64();
  result<std::string> read_utf();

  result<std::vector<int8_t>> bytes(size_t start, size_t end);

  const std::vector<int8_t>& data() const;
  std::vector<int8_t>& data();

  size_t cursor() const;
  [[nodiscard]] bool cursor(size_t cursor);
  [[nodiscard]] bool skip(size_t count);

private:
  std::vector<int8_t> buffer_;
  std::vector<uint8_t> byte_cache_;
  size_t cursor_ = 0;
};

}