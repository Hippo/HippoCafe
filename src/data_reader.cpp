#include "cafe/data_reader.hpp"

#include <stdexcept>

namespace cafe {

data_reader::data_reader(std::vector<int8_t>&& data) : buffer_(std::move(data)) {
}
data_reader::data_reader(const std::vector<int8_t>& data) : buffer_(data) {
}
data_reader::data_reader(std::istream& stream) : buffer_(std::istreambuf_iterator(stream), std::istreambuf_iterator<char>()) {
}
uint8_t data_reader::read_u8() {
  if (cursor_ >= buffer_.size()) {
    throw std::runtime_error("Unexpected end of input");
  }
  return static_cast<uint8_t>(buffer_[cursor_++]);
}
int8_t data_reader::read_i8() {
  if (cursor_ >= buffer_.size()) {
    throw std::runtime_error("Unexpected end of input");
  }
  return buffer_[cursor_++];
}
uint16_t data_reader::read_u16() {
  return static_cast<uint16_t>(read_u8()) << 8 | read_u8();
}
int16_t data_reader::read_i16() {
  return static_cast<int16_t>(read_u16());
}
uint32_t data_reader::read_u32() {
  return static_cast<uint32_t>(read_u8()) << 24 | static_cast<uint32_t>(read_u8()) << 16 |
         static_cast<uint32_t>(read_u8()) << 8 | read_u8();
}
int32_t data_reader::read_i32() {
  return static_cast<int32_t>(read_u32());
}
uint64_t data_reader::read_u64() {
  return static_cast<uint64_t>(read_u8()) << 56 | static_cast<uint64_t>(read_u8()) << 48 |
         static_cast<uint64_t>(read_u8()) << 40 | static_cast<uint64_t>(read_u8()) << 32 |
         static_cast<uint64_t>(read_u8()) << 24 | static_cast<uint64_t>(read_u8()) << 16 |
         static_cast<uint64_t>(read_u8()) << 8 | read_u8();
}
int64_t data_reader::read_i64() {
  return static_cast<int64_t>(read_u64());
}
float data_reader::read_f32() {
  const auto i = read_i32();
  float f;
  std::memcpy(&f, &i, sizeof(float));
  return f;
}
double data_reader::read_f64() {
  const auto i = read_i64();
  double d;
  std::memcpy(&d, &i, sizeof(double));
  return d;
}
std::string data_reader::read_utf() {
  const auto utflen = read_u16();
  if (byte_cache_.capacity() < utflen) {
    byte_cache_.reserve(utflen);
  }
  byte_cache_.clear();
  for (auto i = 0; i < utflen; i++) {
    byte_cache_.emplace_back(read_u8());
  }
  std::string char_cache;
  char_cache.reserve(utflen);
  for (int i = 0; i < utflen; i++) {
    if (const uint8_t byte1 = byte_cache_[i]; (byte1 & 0x80) == 0) {
      char_cache.push_back(static_cast<char>(byte1));
    } else if ((byte1 & 0xE0) == 0xC0) {
      if (i + 1 >= utflen) {
        throw std::runtime_error("Unexpected end of input during 2-byte encoding");
      }
      if (const uint8_t byte2 = byte_cache_[++i]; byte1 == 0xC0 && byte2 == 0x80) {
        char_cache.push_back('\0');
      } else {
        char_cache.push_back(static_cast<char>(byte1));
        char_cache.push_back(static_cast<char>(byte2));
      }
    } else if ((byte1 & 0xF0) == 0xE0) {
      if (i + 2 >= utflen) {
        throw std::runtime_error("Unexpected end of input during 3-byte encoding");
      }
      const uint8_t byte2 = byte_cache_[++i];
      const uint8_t byte3 = byte_cache_[++i];
      if (i + 3 < utflen && byte1 == 0xED && (byte2 & 0xF0) == 0xA0) {
        const uint8_t byte4 = byte_cache_[i + 1];
        const uint8_t byte5 = byte_cache_[i + 2];
        const uint8_t byte6 = byte_cache_[i + 3];
        if (byte4 == 0xED && (byte5 & 0xF0) == 0xB0) {
          i += 3;
          const uint32_t bits = ((byte2 & 0x0F) + 1) << 16 | (byte3 & 0x3F) << 10 | (byte5 & 0x0F) << 6 | byte6 & 0x3F;
          char_cache.push_back(static_cast<char>(0xF0 + (bits >> 18 & 0x07)));
          char_cache.push_back(static_cast<char>(0x80 + (bits >> 12 & 0x3F)));
          char_cache.push_back(static_cast<char>(0x80 + (bits >> 6 & 0x3F)));
          char_cache.push_back(static_cast<char>(0x80 + (bits & 0x3F)));
          continue;
        }
      }
      char_cache.push_back(static_cast<char>(byte1));
      char_cache.push_back(static_cast<char>(byte2));
      char_cache.push_back(static_cast<char>(byte3));
    } else {
      throw std::runtime_error("Invalid UTF-8 encoding");
    }
  }

  return char_cache;
}
const std::vector<int8_t>& data_reader::data() const {
  return buffer_;
}
std::vector<int8_t>& data_reader::data() {
  return buffer_;
}
size_t data_reader::cursor() const {
  return cursor_;
}
void data_reader::cursor(size_t cursor) {
  if (cursor > buffer_.size()) {
    throw std::runtime_error("Cursor out of bounds");
  }
  cursor_ = cursor;
}
void data_reader::skip(size_t count) {
  if (cursor_ + count > buffer_.size()) {
    throw std::runtime_error("Cursor out of bounds");
  }
  cursor_ += count;
}

} // namespace cafe
