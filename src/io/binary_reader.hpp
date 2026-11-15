#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace cafe::detail {

class BinaryReader {
public:
  explicit BinaryReader(std::span<const std::byte> input, std::size_t origin = 0) noexcept
      : input(input.first(std::min(input.size(), std::numeric_limits<std::size_t>::max() - origin))),
        origin(origin) {
  }

  [[nodiscard]] std::size_t offset() const noexcept {
    return origin + cursor;
  }
  [[nodiscard]] std::size_t position() const noexcept {
    return cursor;
  }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return input.size() - cursor;
  }
  [[nodiscard]] std::span<const std::byte> remaining_bytes() const noexcept {
    return input.subspan(cursor);
  }

  [[nodiscard]] std::optional<std::span<const std::byte>> take_slice(std::size_t size) noexcept {
    if (size > remaining())
      return std::nullopt;
    const auto slice = input.subspan(cursor, size);
    cursor += size;
    return slice;
  }

  [[nodiscard]] std::optional<BinaryReader> sub_reader(std::size_t size) noexcept {
    const auto start = offset();
    if (const auto slice = take_slice(size))
      return BinaryReader(*slice, start);
    return std::nullopt;
  }

  [[nodiscard]] std::optional<std::uint8_t> u1() noexcept {
    return read_unsigned<std::uint8_t>();
  }
  [[nodiscard]] std::optional<std::uint16_t> u2() noexcept {
    return read_unsigned<std::uint16_t>();
  }
  [[nodiscard]] std::optional<std::uint32_t> u4() noexcept {
    return read_unsigned<std::uint32_t>();
  }
  [[nodiscard]] std::optional<std::uint64_t> u8() noexcept {
    return read_unsigned<std::uint64_t>();
  }
  [[nodiscard]] std::optional<std::int8_t> s1() noexcept {
    return read_signed<std::int8_t, std::uint8_t>();
  }
  [[nodiscard]] std::optional<std::int16_t> s2() noexcept {
    return read_signed<std::int16_t, std::uint16_t>();
  }
  [[nodiscard]] std::optional<std::int32_t> s4() noexcept {
    return read_signed<std::int32_t, std::uint32_t>();
  }
  [[nodiscard]] std::optional<std::int64_t> s8() noexcept {
    return read_signed<std::int64_t, std::uint64_t>();
  }

private:
  template<typename Value>
  [[nodiscard]] std::optional<Value> read_unsigned() noexcept {
    const auto slice = take_slice(sizeof(Value));
    if (!slice)
      return std::nullopt;
    Value value = 0;
    for (const auto byte : *slice)
      value = static_cast<Value>((value << 8) | std::to_integer<std::uint8_t>(byte));
    return value;
  }

  template<typename Signed, typename Unsigned>
  [[nodiscard]] std::optional<Signed> read_signed() noexcept {
    if (const auto value = read_unsigned<Unsigned>())
      return std::bit_cast<Signed>(*value);
    return std::nullopt;
  }

  std::span<const std::byte> input;
  std::size_t origin;
  std::size_t cursor = 0;
};

} // namespace cafe::detail
