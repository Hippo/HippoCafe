#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace cafe {

class data_writer {
public:
  data_writer() = default;
  ~data_writer() = default;
  data_writer(const data_writer&) = default;
  data_writer(data_writer&&) = default;
  data_writer& operator=(const data_writer&) = default;
  data_writer& operator=(data_writer&&) = default;

  static void write_i8(std::vector<int8_t>& buffer, int8_t value);
  static void write_u8(std::vector<int8_t>& buffer, uint8_t value);
  static void write_i16(std::vector<int8_t>& buffer, int16_t value);
  static void write_u16(std::vector<int8_t>& buffer, uint16_t value);
  static void write_i32(std::vector<int8_t>& buffer, int32_t value);
  static void write_u32(std::vector<int8_t>& buffer, uint32_t value);
  static void write_i64(std::vector<int8_t>& buffer, int64_t value);
  static void write_u64(std::vector<int8_t>& buffer, uint64_t value);
  static void write_f32(std::vector<int8_t>& buffer, float value);
  static void write_f64(std::vector<int8_t>& buffer, double value);
  static void write_utf(std::vector<int8_t>& buffer, const std::string_view& value);


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

  const std::vector<int8_t>& data() const;
  std::vector<int8_t>& data();

private:
  std::vector<int8_t> buffer_;
};

class databuf {
public:
  explicit databuf(std::vector<int8_t>& buffer);
  ~databuf() = default;
  databuf(const databuf&) = delete;
  databuf(databuf&&) = default;
  databuf& operator=(const databuf&) = delete;
  databuf& operator=(databuf&&) = delete;

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
private:
  std::vector<int8_t>& buffer_;
};

} // namespace cafe
