#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cafe::detail {

template<typename Buffer>
class BinaryWriter {
public:
  explicit BinaryWriter(Buffer& output) noexcept : output(output) {
  }

  void u1(std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
  }
  void u2(std::uint16_t value) {
    write_unsigned(value);
  }
  void u4(std::uint32_t value) {
    write_unsigned(value);
  }
  void u8(std::uint64_t value) {
    write_unsigned(value);
  }
  void s1(std::int8_t value) {
    u1(std::bit_cast<std::uint8_t>(value));
  }
  void s2(std::int16_t value) {
    u2(std::bit_cast<std::uint16_t>(value));
  }
  void s4(std::int32_t value) {
    u4(std::bit_cast<std::uint32_t>(value));
  }
  void s8(std::int64_t value) {
    u8(std::bit_cast<std::uint64_t>(value));
  }

  void bytes(std::span<const std::byte> value) {
    if (!value.empty())
      output.insert(output.end(), value.begin(), value.end());
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return output.size();
  }

  [[nodiscard]] bool patch_u4(std::size_t offset, std::uint32_t value) noexcept {
    if (offset > size() || 4 > size() - offset)
      return false;
    for (unsigned i = 0; i < 4; ++i)
      output[offset + i] = static_cast<std::byte>((value >> ((3 - i) * 8)) & 0xffu);
    return true;
  }

private:
  template<typename Value>
  void write_unsigned(Value value) {
    for (std::size_t i = sizeof(Value); i > 0; --i)
      u1(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
  }

  Buffer& output;
};

} // namespace cafe::detail
