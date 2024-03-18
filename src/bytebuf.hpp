#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace cafe {

class bytebuf {
public:
  bytebuf() = default;
  ~bytebuf() = default;
  bytebuf(const bytebuf&) = default;
  bytebuf(bytebuf&&) = default;
  bytebuf& operator=(const bytebuf&) = default;
  bytebuf& operator=(bytebuf&&) = default;

  void write_i8(int8_t value);
  void write_u8(uint8_t value);
  void write_i16(int16_t value);
  void write_u16(uint16_t value);
  void write_i32(int32_t value);
  void write_u32(uint32_t value);
  void write_i64(int64_t value);
  void write_u64(uint64_t value);
  void write_f32(float value);
  void write_f64(double value);
  void write_utf(const std::string_view& value);

  template<typename Iterator>
  void write_all(const Iterator& data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
  }

  [[nodiscard]] const std::vector<int8_t>& data() const;
  [[nodiscard]] std::vector<int8_t>& data();

private:
  std::vector<int8_t> buffer_;
};

} // namespace cafe
