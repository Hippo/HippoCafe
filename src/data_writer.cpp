#include "cafe/data_writer.hpp"

#include <cmath>

namespace cafe {
inline size_t to_utf8m_len(const std::string_view& input) {
  size_t new_length = 0;
  const auto length = input.length();

  for (auto i = 0; i < length; i++) {
    if (const auto byte = static_cast<uint8_t>(input[i]); (byte & 0x80) == 0) {
      new_length += 1 + (byte == 0);
    } else if ((byte & 0xE0) == 0xC0) {
      if (i + 1 >= length || (input[i + 1] & 0xC0) != 0x80) {
        return length;
      }
      i++;
      new_length += 2;
    } else if ((byte & 0xF0) == 0xE0) {
      if (i + 2 >= length || (input[i + 1] & 0xC0) != 0x80 || (input[i + 2] & 0xC0) != 0x80) {
        return length;
      }
      i += 2;
      new_length += 3;
    } else if ((byte & 0xF8) == 0xF0) {
      if (i + 3 >= length || (input[i + 1] & 0xC0) != 0x80 || (input[i + 2] & 0xC0) != 0x80 ||
          (input[i + 3] & 0xC0) != 0x80) {
        return length;
      }
      i += 3;
      new_length += 6;
    } else {
      return length;
    }
  }
  return new_length;
}
void data_writer::write_i8(std::vector<int8_t>& buffer, int8_t value) {
  buffer.emplace_back(value);
}
void data_writer::write_u8(std::vector<int8_t>& buffer, uint8_t value) {
  buffer.emplace_back(static_cast<int8_t>(value));
}
void data_writer::write_i16(std::vector<int8_t>& buffer, int16_t value) {
  write_u16(buffer, static_cast<uint16_t>(value));
}
void data_writer::write_u16(std::vector<int8_t>& buffer, uint16_t value) {
  const auto b1 = static_cast<int8_t>(value >> 8);
  const auto b2 = static_cast<int8_t>(value & 0xFF);
  write_i8(buffer, b1);
  write_i8(buffer, b2);
}
void data_writer::write_i32(std::vector<int8_t>& buffer, int32_t value) {
  write_u32(buffer, static_cast<uint32_t>(value));
}
void data_writer::write_u32(std::vector<int8_t>& buffer, uint32_t value) {
  const auto b1 = static_cast<int8_t>(value >> 24);
  const auto b2 = static_cast<int8_t>(value >> 16 & 0xFF);
  const auto b3 = static_cast<int8_t>(value >> 8 & 0xFF);
  const auto b4 = static_cast<int8_t>(value & 0xFF);
  write_i8(buffer, b1);
  write_i8(buffer, b2);
  write_i8(buffer, b3);
  write_i8(buffer, b4);
}
void data_writer::write_i64(std::vector<int8_t>& buffer, int64_t value) {
  write_u64(buffer, static_cast<uint64_t>(value));
}
void data_writer::write_u64(std::vector<int8_t>& buffer, uint64_t value) {
  const auto b1 = static_cast<int8_t>(value >> 56);
  const auto b2 = static_cast<int8_t>(value >> 48 & 0xFF);
  const auto b3 = static_cast<int8_t>(value >> 40 & 0xFF);
  const auto b4 = static_cast<int8_t>(value >> 32 & 0xFF);
  const auto b5 = static_cast<int8_t>(value >> 24 & 0xFF);
  const auto b6 = static_cast<int8_t>(value >> 16 & 0xFF);
  const auto b7 = static_cast<int8_t>(value >> 8 & 0xFF);
  const auto b8 = static_cast<int8_t>(value & 0xFF);
  write_i8(buffer, b1);
  write_i8(buffer, b2);
  write_i8(buffer, b3);
  write_i8(buffer, b4);
  write_i8(buffer, b5);
  write_i8(buffer, b6);
  write_i8(buffer, b7);
  write_i8(buffer, b8);
}
void data_writer::write_f32(std::vector<int8_t>& buffer, float value) {
  if (std::isnan(value)) {
    write_i32(buffer, 0x7fc00000);
  } else {
    int32_t i;
    std::memcpy(&i, &value, sizeof(float));
    write_i32(buffer, i);
  }
}
void data_writer::write_f64(std::vector<int8_t>& buffer, double value) {
  if (std::isnan(value)) {
    write_i64(buffer, 0x7ff8000000000000L);
  } else {
    int64_t i;
    std::memcpy(&i, &value, sizeof(double));
    write_i64(buffer, i);
  }
}
void data_writer::write_utf(std::vector<int8_t>& buffer, const std::string_view& value) {
  const auto new_length = to_utf8m_len(value);
  const auto length = value.length();

  write_u16(buffer, static_cast<uint16_t>(new_length));

  for (auto i = 0; i < length; i++) {
    if (const auto byte = static_cast<uint8_t>(value[i]); (byte & 0x80) == 0) {
      if (byte == '\0') {
        write_i8(buffer, static_cast<int8_t>(0xC0));
        write_i8(buffer, static_cast<int8_t>(0x80));
      } else {
        write_i8(buffer, static_cast<int8_t>(byte));
      }
    } else if ((byte & 0xE0) == 0xC0) {
      write_i8(buffer, static_cast<int8_t>(byte));
      write_i8(buffer, static_cast<int8_t>(value[++i]));
    } else if ((byte & 0xF0) == 0xE0) {
      write_i8(buffer, static_cast<int8_t>(byte));
      write_i8(buffer, static_cast<int8_t>(value[++i]));
      write_i8(buffer, static_cast<int8_t>(value[++i]));
    } else if ((byte & 0xF8) == 0xF0) {
      const auto byte2 = static_cast<uint8_t>(value[++i]);
      const auto byte3 = static_cast<uint8_t>(value[++i]);
      const auto byte4 = static_cast<uint8_t>(value[++i]);
      const auto bits = (byte & 0x07) << 18 | (byte2 & 0x3F) << 12 | (byte3 & 0x3F) << 6 | byte4 & 0x3F;

      write_i8(buffer, static_cast<int8_t>(0xED));
      write_i8(buffer, static_cast<int8_t>(0xA0 + ((bits >> 16) - 1 & 0x0F)));
      write_i8(buffer, static_cast<int8_t>(0x80 + (bits >> 10 & 0x3F)));
      write_i8(buffer, static_cast<int8_t>(0xED));
      write_i8(buffer, static_cast<int8_t>(0xB0 + (bits >> 6 & 0x0F)));
      write_i8(buffer, static_cast<int8_t>(byte4));
    }
  }
}

void data_writer::write_i8(int8_t value) {
  return write_u8(buffer_, value);
}
void data_writer::write_u8(uint8_t value) {
  return write_u8(buffer_, value);
}
void data_writer::write_i16(int16_t value) {
  return write_i16(buffer_, value);
}
void data_writer::write_u16(uint16_t value) {
  return write_u16(buffer_, value);
}
void data_writer::write_i32(int32_t value) {
  return write_i32(buffer_, value);
}
void data_writer::write_u32(uint32_t value) {
  return write_u32(buffer_, value);
}
void data_writer::write_i64(int64_t value) {
  return write_i64(buffer_, value);
}
void data_writer::write_u64(uint64_t value) {
  return write_u64(buffer_, value);
}
void data_writer::write_f32(float value) {
  return write_f32(buffer_, value);
}
void data_writer::write_f64(double value) {
  return write_f64(buffer_, value);
}
void data_writer::write_utf(const std::string_view& value) {
  return write_utf(buffer_, value);
}
const std::vector<int8_t>& data_writer::data() const {
  return buffer_;
}
std::vector<int8_t>& data_writer::data() {
  return buffer_;
}
databuf::databuf(std::vector<int8_t>& buffer) : buffer_(buffer) {
}
void databuf::write_i8(int8_t value) {
  return data_writer::write_i8(buffer_, value);
}
void databuf::write_u8(uint8_t value) {
  return data_writer::write_u8(buffer_, value);
}
void databuf::write_i16(int16_t value) {
  return data_writer::write_i16(buffer_, value);
}
void databuf::write_u16(uint16_t value) {
  return data_writer::write_u16(buffer_, value);
}
void databuf::write_i32(int32_t value) {
  return data_writer::write_i32(buffer_, value);
}
void databuf::write_u32(uint32_t value) {
  return data_writer::write_u32(buffer_, value);
}
void databuf::write_i64(int64_t value) {
  return data_writer::write_i64(buffer_, value);
}
void databuf::write_u64(uint64_t value) {
  return data_writer::write_u64(buffer_, value);
}
void databuf::write_f32(float value) {
  return data_writer::write_f32(buffer_, value);
}
void databuf::write_f64(double value) {
  return data_writer::write_f64(buffer_, value);
}
void databuf::write_utf(const std::string_view& value) {
  return data_writer::write_utf(buffer_, value);
}
} // namespace cafe
