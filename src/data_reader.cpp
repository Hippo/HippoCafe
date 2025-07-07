#include "cafe/data_reader.hpp"

#include <cstring>
#include <iostream>

namespace cafe {

data_reader::data_reader(std::vector<int8_t>&& data) : buffer_(std::move(data)) {
}
data_reader::data_reader(const std::vector<int8_t>& data) : buffer_(data) {
}
data_reader::data_reader(std::istream& stream) :
    buffer_(std::istreambuf_iterator(stream), std::istreambuf_iterator<char>()) {
}
result<uint8_t> data_reader::read_u8() {
  if (cursor_ >= buffer_.size()) {
    return error("Unexpected end of input");
  }
  return static_cast<uint8_t>(buffer_[cursor_++]);
}
result<int8_t> data_reader::read_i8() {
  if (cursor_ >= buffer_.size()) {
    return error("Unexpected end of input");
  }
  return buffer_[cursor_++];
}
result<uint16_t> data_reader::read_u16() {
  const auto b1 = read_u8();
  const auto b2 = read_u8();
  return b1.lift([](uint8_t b1, uint8_t b2) -> uint16_t { return static_cast<uint16_t>(b1) << 8 | b2; }, b2);
}
result<int16_t> data_reader::read_i16() {
  return read_u16().map([](uint16_t value) { return static_cast<int16_t>(value); });
}
result<uint32_t> data_reader::read_u32() {
  const auto b1 = read_u8();
  const auto b2 = read_u8();
  const auto b3 = read_u8();
  const auto b4 = read_u8();
  return b1.lift(
      [](uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4) -> uint32_t {
        return static_cast<uint32_t>(b1) << 24 | static_cast<uint32_t>(b2) << 16 | static_cast<uint32_t>(b3) << 8 | b4;
      },
      b2, b3, b4);
}
result<int32_t> data_reader::read_i32() {
  return read_u32().map([](uint32_t value) { return static_cast<int32_t>(value); });
}
result<uint64_t> data_reader::read_u64() {
  const auto b1 = read_u8();
  const auto b2 = read_u8();
  const auto b3 = read_u8();
  const auto b4 = read_u8();
  const auto b5 = read_u8();
  const auto b6 = read_u8();
  const auto b7 = read_u8();
  const auto b8 = read_u8();
  return b1.lift(
      [](uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7, uint8_t b8) -> uint64_t {
        return static_cast<uint64_t>(b1) << 56 | static_cast<uint64_t>(b2) << 48 | static_cast<uint64_t>(b3) << 40 |
               static_cast<uint64_t>(b4) << 32 | static_cast<uint64_t>(b5) << 24 | static_cast<uint64_t>(b6) << 16 |
               static_cast<uint64_t>(b7) << 8 | b8;
      },
      b2, b3, b4, b5, b6, b7, b8);
}
result<int64_t> data_reader::read_i64() {
  return read_u64().map([](uint64_t value) { return static_cast<int64_t>(value); });
}
result<float> data_reader::read_f32() {
  return read_i32().map([](int32_t i) {
    float f;
    std::memcpy(&f, &i, sizeof(float));
    return f;
  });
}
result<double> data_reader::read_f64() {
  return read_i64().map([](int64_t i) {
    double d;
    std::memcpy(&d, &i, sizeof(double));
    return d;
  });
}
result<std::string> data_reader::read_utf() {
  const auto utflen_res = read_u16();
  if (!utflen_res) {
    return utflen_res.err();
  }
  const auto utflen = utflen_res.value();
  if (byte_cache_.capacity() < utflen) {
    byte_cache_.reserve(utflen);
  }
  byte_cache_.clear();
  for (auto i = 0; i < utflen; i++) {
    const auto byte_res = read_u8();
    if (!byte_res) {
      return byte_res.err();
    }
    byte_cache_.emplace_back(byte_res.value());
  }
  std::string char_cache;
  char_cache.reserve(utflen);
  for (int i = 0; i < utflen; i++) {
    if (const uint8_t byte1 = byte_cache_[i]; (byte1 & 0x80) == 0) {
      char_cache.push_back(static_cast<char>(byte1));
    } else if ((byte1 & 0xE0) == 0xC0) {
      if (i + 1 >= utflen) {
        return error("Unexpected end of input during 2-byte encoding");
      }
      if (const uint8_t byte2 = byte_cache_[++i]; byte1 == 0xC0 && byte2 == 0x80) {
        char_cache.push_back('\0');
      } else {
        char_cache.push_back(static_cast<char>(byte1));
        char_cache.push_back(static_cast<char>(byte2));
      }
    } else if ((byte1 & 0xF0) == 0xE0) {
      if (i + 2 >= utflen) {
        return error("Unexpected end of input during 3-byte encoding");
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
      return error("Invalid UTF-8 encoding");
    }
  }

  return char_cache;
}

result<std::vector<int8_t>> data_reader::bytes(size_t start, size_t end) {
  if (start > buffer_.size() || end > buffer_.size() || start > end) {
    return error("Invalid range");
  }
  return std::vector(buffer_.begin() + static_cast<std::vector<int8_t>::difference_type>(start),
                     buffer_.begin() + static_cast<std::vector<int8_t>::difference_type>(end));
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
bool data_reader::cursor(size_t cursor) {
  if (cursor > buffer_.size()) {
    return false;
  }
  cursor_ = cursor;
  return true;
}
bool data_reader::skip(size_t count) {
  if (cursor_ + count > buffer_.size()) {
    return false;
  }
  cursor_ += count;
  return true;
}

} // namespace cafe
