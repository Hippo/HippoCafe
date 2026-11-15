/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace cafe::detail {

/**
 * @brief Non-owning bounded big-endian input cursor. Failed fixed-width reads leave the cursor
 * unchanged; subreaders retain absolute diagnostic origins.
 * @ingroup implementation
 */
class BinaryReader {
public:
  /**
   * @brief Initialize non-owning bounded big-endian input cursor
   * @param input Borrowed bounded bytes; their storage must outlive this reader and its subreaders.
   * @param origin Absolute source-byte origin used for diagnostics.
   */
  explicit BinaryReader(std::span<const std::byte> input, std::size_t origin = 0) noexcept
      : input(input.first(std::min(input.size(), std::numeric_limits<std::size_t>::max() - origin))),
        origin(origin) {
  }

  /**
   * @brief Read the absolute source offset of the current cursor.
   * @return Origin plus bytes consumed.
   */
  [[nodiscard]] std::size_t offset() const noexcept {
    return origin + cursor;
  }
  /**
   * @brief Read the cursor relative to this reader's bounded span.
   * @return Bytes consumed from this reader's input.
   */
  [[nodiscard]] std::size_t position() const noexcept {
    return cursor;
  }
  /**
   * @brief Count bytes not yet consumed.
   * @return The bounded remaining byte count.
   */
  [[nodiscard]] std::size_t remaining() const noexcept {
    return input.size() - cursor;
  }
  /**
   * @brief Borrow the unconsumed tail without moving the cursor.
   * @return A span borrowing the original input.
   */
  [[nodiscard]] std::span<const std::byte> remaining_bytes() const noexcept {
    return input.subspan(cursor);
  }

  /**
   * @brief Consume an exact byte count only when it fits the remaining input.
   * @param size Requested byte count or checked input byte length.
   * @return The borrowed slice, or nullopt with the cursor unchanged.
   */
  [[nodiscard]] std::optional<std::span<const std::byte>> take_slice(std::size_t size) noexcept {
    if (size > remaining())
      return std::nullopt;
    const auto slice = input.subspan(cursor, size);
    cursor += size;
    return slice;
  }

  /**
   * @brief Consume a bounded subrange and give it the corresponding absolute origin.
   * @param size Requested byte count or checked input byte length.
   * @return A child reader, or nullopt with the cursor unchanged.
   */
  [[nodiscard]] std::optional<BinaryReader> sub_reader(std::size_t size) noexcept {
    const auto start = offset();
    if (const auto slice = take_slice(size))
      return BinaryReader(*slice, start);
    return std::nullopt;
  }

  /**
   * @brief Consume a 1-byte unsigned big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::uint8_t> u1() noexcept {
    return read_unsigned<std::uint8_t>();
  }
  /**
   * @brief Consume a 2-byte unsigned big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::uint16_t> u2() noexcept {
    return read_unsigned<std::uint16_t>();
  }
  /**
   * @brief Consume a 4-byte unsigned big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::uint32_t> u4() noexcept {
    return read_unsigned<std::uint32_t>();
  }
  /**
   * @brief Consume a 8-byte unsigned big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::uint64_t> u8() noexcept {
    return read_unsigned<std::uint64_t>();
  }
  /**
   * @brief Consume a 1-byte signed big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::int8_t> s1() noexcept {
    return read_signed<std::int8_t, std::uint8_t>();
  }
  /**
   * @brief Consume a 2-byte signed big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::int16_t> s2() noexcept {
    return read_signed<std::int16_t, std::uint16_t>();
  }
  /**
   * @brief Consume a 4-byte signed big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::int32_t> s4() noexcept {
    return read_signed<std::int32_t, std::uint32_t>();
  }
  /**
   * @brief Consume a 8-byte signed big-endian integer.
   * @return The decoded value, or nullopt with the cursor unchanged on truncation.
   */
  [[nodiscard]] std::optional<std::int64_t> s8() noexcept {
    return read_signed<std::int64_t, std::uint64_t>();
  }

private:
  /**
   * @brief Consume one unsigned big-endian value atomically.
   * @return The decoded value, or nullopt when insufficient bytes remain.
   */
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

  /**
   * @brief Consume a signed big-endian value by preserving its bit representation.
   * @return The decoded value, or nullopt when insufficient bytes remain.
   */
  template<typename Signed, typename Unsigned>
  [[nodiscard]] std::optional<Signed> read_signed() noexcept {
    if (const auto value = read_unsigned<Unsigned>())
      return std::bit_cast<Signed>(*value);
    return std::nullopt;
  }

  /**
   * @brief Borrowed bounded byte span; its owner must outlive this reader.
   */
  std::span<const std::byte> input;
  std::size_t origin; ///< Byte origin used to translate logical offsets to the original input.
  std::size_t cursor = 0; ///< Current byte position within the bounded logical input.
};

} // namespace cafe::detail
