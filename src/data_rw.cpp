#include "cafe/data_rw.hpp"

#include <stdexcept>
#include <memory>

namespace cafe {
inline size_t to_utf8m_len(const std::string_view& input) {
  size_t new_length = 0;
  const auto length = input.length();

  for (auto i = 0; i < length; i++) {
    if (const auto byte = static_cast<uint8_t>(input[i]); (byte & 0x80) == 0) {
      new_length += 1 + (byte == 0);
    } else if ((byte & 0xE0) == 0xC0) {
      if (i + 1 >= length || (input[i + 1] & 0xC0) != 0x80) {
        throw std::runtime_error("Unexpected end of input during 2-byte encoding");
      }
      i++;
      new_length += 2;
    } else if ((byte & 0xF0) == 0xE0) {
      if (i + 2 >= length || (input[i + 1] & 0xC0) != 0x80 || (input[i + 2] & 0xC0) != 0x80) {
        throw std::runtime_error("Unexpected end of input during 3-byte encoding");
      }
      i += 2;
      new_length += 3;
    } else if ((byte & 0xF8) == 0xF0) {
      if (i + 3 >= length || (input[i + 1] & 0xC0) != 0x80 || (input[i + 2] & 0xC0) != 0x80 || (input[i + 3] & 0xC0) !=
          0x80) {
        throw std::runtime_error("Unexpected end of input during 4-byte encoding");
      }
      i += 3;
      new_length += 6;
    } else {
      throw std::runtime_error("Invalid UTF-8 encoding");
    }
  }
  return new_length;
}

data_reader::data_reader(std::istream& stream) : stream_(stream) {
  byte_cache_.reserve(128);
}

int8_t data_reader::read_i8() {
  return static_cast<int8_t>(read_u8());
}

uint8_t data_reader::read_u8() {
  uint8_t byte;
  stream_.read(reinterpret_cast<char*>(&byte), 1);
  return byte;
}

int16_t data_reader::read_i16() {
  return static_cast<int16_t>(read_u16());
}

uint16_t data_reader::read_u16() {
  return static_cast<uint16_t>(read_u8()) << 8 | read_u8();
}

int32_t data_reader::read_i32() {
  return static_cast<int32_t>(read_u32());
}

uint32_t data_reader::read_u32() {
  return static_cast<uint32_t>(read_u8()) << 24 | static_cast<uint32_t>(read_u8()) << 16 |
         static_cast<uint32_t>(read_u8()) << 8 | read_u8();
}

int64_t data_reader::read_i64() {
  return static_cast<int64_t>(read_u64());
}

uint64_t data_reader::read_u64() {
  return static_cast<uint64_t>(read_u8()) << 56 | static_cast<uint64_t>(read_u8()) << 48 |
         static_cast<uint64_t>(read_u8()) << 40 | static_cast<uint64_t>(read_u8()) << 32 |
         static_cast<uint64_t>(read_u8()) << 24 | static_cast<uint64_t>(read_u8()) << 16 |
         static_cast<uint64_t>(read_u8()) << 8 | read_u8();
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
    byte_cache_.push_back(read_i8());
  }
  std::string char_cache;
  char_cache.reserve(utflen);
  for (int i = 0; i < utflen; ++i) {
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

void data_reader::read_bytes(std::vector<uint8_t>& bytes, size_t length) {
  bytes.reserve(bytes.size() + length);
  for (auto i = 0; i < length; i++) {
    bytes.push_back(read_u8());
  }
}

void data_reader::read_bytes(std::vector<int8_t>& bytes, size_t length) {
  bytes.reserve(bytes.size() + length);
  for (auto i = 0; i < length; i++) {
    bytes.push_back(read_i8());
  }
}

void data_reader::read_shorts(std::vector<uint16_t>& shorts, size_t length) {
  shorts.reserve(shorts.size() + length);
  for (auto i = 0; i < length; i++) {
    shorts.push_back(read_u16());
  }
}

data_writer::data_writer(std::ostream& stream) : stream_(stream) {}

void data_writer::write_i8(int8_t value) {
  stream_.write(reinterpret_cast<const char*>(&value), 1);
}

void data_writer::write_u8(uint8_t value) {
  stream_.write(reinterpret_cast<const char*>(&value), 1);
}

void data_writer::write_i16(int16_t value) {
  write_u16(static_cast<uint16_t>(value));
}

void data_writer::write_u16(uint16_t value) {
  const auto b1 = static_cast<int8_t>(value >> 8);
  const auto b2 = static_cast<int8_t>(value & 0xFF);
  write_i8(b1);
  write_i8(b2);
}

void data_writer::write_i32(int32_t value) {
  write_u32(static_cast<uint32_t>(value));
}

void data_writer::write_u32(uint32_t value) {
  const auto b1 = static_cast<int8_t>(value >> 24);
  const auto b2 = static_cast<int8_t>(value >> 16 & 0xFF);
  const auto b3 = static_cast<int8_t>(value >> 8 & 0xFF);
  const auto b4 = static_cast<int8_t>(value & 0xFF);
  write_i8(b1);
  write_i8(b2);
  write_i8(b3);
  write_i8(b4);
}

void data_writer::write_i64(int64_t value) {
  write_u64(static_cast<uint64_t>(value));
}

void data_writer::write_u64(uint64_t value) {
  const auto b1 = static_cast<int8_t>(value >> 56);
  const auto b2 = static_cast<int8_t>(value >> 48 & 0xFF);
  const auto b3 = static_cast<int8_t>(value >> 40 & 0xFF);
  const auto b4 = static_cast<int8_t>(value >> 32 & 0xFF);
  const auto b5 = static_cast<int8_t>(value >> 24 & 0xFF);
  const auto b6 = static_cast<int8_t>(value >> 16 & 0xFF);
  const auto b7 = static_cast<int8_t>(value >> 8 & 0xFF);
  const auto b8 = static_cast<int8_t>(value & 0xFF);
  write_i8(b1);
  write_i8(b2);
  write_i8(b3);
  write_i8(b4);
  write_i8(b5);
  write_i8(b6);
  write_i8(b7);
  write_i8(b8);
}

void data_writer::write_f32(float value) {
  int32_t i;
  std::memcpy(&i, &value, sizeof(float));
  write_u32(i);
}

void data_writer::write_f64(double value) {
  int64_t i;
  std::memcpy(&i, &value, sizeof(double));
  write_u64(i);
}


void data_writer::write_utf(const std::string_view& value) {
  const auto new_length = to_utf8m_len(value);
  const auto length = value.length();

  write_i8(static_cast<int8_t>(new_length >> 8));
  write_i8(static_cast<int8_t>(new_length & 0xFF));

  for (auto i = 0; i < length; i++) {
    if (const auto byte = static_cast<uint8_t>(value[i]); (byte & 0x80) == 0) {
      if (byte == '\0') {
        write_i8(static_cast<int8_t>(0xC0));
        write_i8(static_cast<int8_t>(0x80));
      } else {
        write_i8(static_cast<int8_t>(byte));
      }
    } else if ((byte & 0xE0) == 0xC0) {
      write_i8(static_cast<int8_t>(byte));
      write_i8(static_cast<int8_t>(value[++i]));
    } else if ((byte & 0xF0) == 0xE0) {
      write_i8(static_cast<int8_t>(byte));
      write_i8(static_cast<int8_t>(value[++i]));
      write_i8(static_cast<int8_t>(value[++i]));
    } else if ((byte & 0xF8) == 0xF0) {
      const auto byte2 = static_cast<uint8_t>(value[++i]);
      const auto byte3 = static_cast<uint8_t>(value[++i]);
      const auto byte4 = static_cast<uint8_t>(value[++i]);
      const auto bits = (byte & 0x07) << 18 | (byte2 & 0x3F) << 12 | (byte3 & 0x3F) << 6 | byte4 & 0x3F;

      write_i8(static_cast<int8_t>(0xED));
      write_i8(static_cast<int8_t>(0xA0 + ((bits >> 16) - 1 & 0x0F)));
      write_i8(static_cast<int8_t>(0x80 + (bits >> 10 & 0x3F)));
      write_i8(static_cast<int8_t>(0xED));
      write_i8(static_cast<int8_t>(0xB0 + (bits >> 6 & 0x0F)));
      write_i8(static_cast<int8_t>(byte4));
    }
  }
}

void data_writer::write_bytes(const std::vector<uint8_t>& bytes) {
  for (const auto& b : bytes) {
    write_u8(b);
  }
}

void data_writer::write_bytes(const std::string_view& bytes) {
  for (const auto& b : bytes) {
    write_i8(b);
  }
}
}
