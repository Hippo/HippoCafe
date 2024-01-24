#ifndef CAFE_DATA_RW_HPP
#define CAFE_DATA_RW_HPP


#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <istream>
#include <ostream>


namespace cafe {
class data_reader {
public:
  data_reader(std::istream& stream);

  ~data_reader() = default;

  data_reader(const data_reader&) = delete;

  data_reader& operator=(const data_reader&) = delete;

  data_reader(data_reader&&) noexcept = delete;

  data_reader& operator=(data_reader&&) noexcept = delete;

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

  void read_bytes(std::vector<uint8_t>& bytes, size_t length);
  void read_bytes(std::vector<int8_t>& bytes, size_t length);
  void read_shorts(std::vector<uint16_t>& shorts, size_t length);

private:
  std::istream& stream_;
  std::vector<int8_t> byte_cache_;
};

class data_writer {
public:
  data_writer(std::ostream& stream);

  ~data_writer() = default;

  data_writer(const data_writer&) = delete;

  data_writer& operator=(const data_writer&) = delete;

  data_writer(data_writer&&) noexcept = delete;

  data_writer& operator=(data_writer&&) noexcept = delete;

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

private:
  std::ostream& stream_;
};
}

#endif //CAFE_DATA_RW_HPP
