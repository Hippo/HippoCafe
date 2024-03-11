#pragma once


#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>


namespace cafe {

template<typename CharT>
class vecstreambuf : public std::streambuf {
public:
  using int_type = std::streambuf::int_type;
  using traits_type = std::streambuf::traits_type;
  vecstreambuf(std::vector<CharT>& vec) : vec_(vec) {
    setg(reinterpret_cast<char*>(vec.data()), reinterpret_cast<char*>(vec.data()),
         reinterpret_cast<char*>(vec.data()) + vec.size());
  }
  ~vecstreambuf() override = default;

  [[nodiscard]] const std::vector<CharT>& vec() const {
    return vec_;
  }

protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(traits_type::eof(), ch)) {
      return traits_type::not_eof(ch);
    }
    vec_.emplace_back(traits_type::to_char_type(ch));
    return ch;
  }

private:
  std::vector<CharT>& vec_;
};

template<typename CharT>
class imemstreambuf : public std::streambuf {
public:
  imemstreambuf(const CharT* data, size_t length) {
    auto* data_ptr = reinterpret_cast<char*>(const_cast<CharT*>(data));
    setg(data_ptr, data_ptr, data_ptr + length);
  }
  ~imemstreambuf() override = default;
};


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

  bool eof() const;

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

  void write_bytes(const std::vector<uint8_t>& bytes);
  void write_bytes(const std::string_view& bytes);

private:
  std::ostream& stream_;
};
} // namespace cafe
