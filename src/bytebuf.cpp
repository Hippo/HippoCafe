#include "bytebuf.hpp"

#include <stdexcept>

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
      if (i + 3 >= length || (input[i + 1] & 0xC0) != 0x80 || (input[i + 2] & 0xC0) != 0x80 ||
          (input[i + 3] & 0xC0) != 0x80) {
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
void bytebuf::write_i8(int8_t value) {
  buffer_.emplace_back(value);
}
void bytebuf::write_u8(uint8_t value) {
  buffer_.emplace_back(static_cast<int8_t>(value));
}
void bytebuf::write_i16(int16_t value) {
  write_u16(static_cast<uint16_t>(value));
}
void bytebuf::write_u16(uint16_t value) {
  const auto b1 = static_cast<int8_t>(value >> 8);
  const auto b2 = static_cast<int8_t>(value & 0xFF);
  write_i8(b1);
  write_i8(b2);
}
void bytebuf::write_i32(int32_t value) {
  write_u32(static_cast<uint32_t>(value));
}
void bytebuf::write_u32(uint32_t value) {
  const auto b1 = static_cast<int8_t>(value >> 24);
  const auto b2 = static_cast<int8_t>(value >> 16 & 0xFF);
  const auto b3 = static_cast<int8_t>(value >> 8 & 0xFF);
  const auto b4 = static_cast<int8_t>(value & 0xFF);
  write_i8(b1);
  write_i8(b2);
  write_i8(b3);
  write_i8(b4);
}
void bytebuf::write_i64(int64_t value) {
  write_u64(static_cast<uint16_t>(value));
}
void bytebuf::write_u64(uint64_t value) {
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
void bytebuf::write_f32(float value) {
  int32_t i;
  std::memcpy(&i, &value, sizeof(float));
  write_i32(i);
}
void bytebuf::write_f64(double value) {
  int64_t i;
  std::memcpy(&i, &value, sizeof(double));
  write_i64(i);
}
void bytebuf::write_utf(const std::string_view& value) {
  const auto new_length = to_utf8m_len(value);
  const auto length = value.length();

  write_u16(static_cast<uint16_t>(new_length));

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
const std::vector<int8_t>& bytebuf::data() const {
  return buffer_;
}
std::vector<int8_t>& bytebuf::data() {
  return buffer_;
}
} // namespace cafe
