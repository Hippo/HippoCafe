/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cafe::detail {

/**
 * @brief Append-only big-endian byte encoder over a caller-owned vector-like container. Patching
 * requires an already emitted range; the container must outlive the writer.
 * @ingroup implementation
 */
template<typename Buffer>
class BinaryWriter {
public:
  /**
   * @brief Initialize append-only big-endian byte encoder over a caller-owned vector-like container
   * @param output Caller-owned output byte container.
   */
  explicit BinaryWriter(Buffer& output) noexcept : output(output) {
  }

  /**
   * @brief Append a 1-byte unsigned integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void u1(std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
  }
  /**
   * @brief Append a 2-byte unsigned integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void u2(std::uint16_t value) {
    write_unsigned(value);
  }
  /**
   * @brief Append a 4-byte unsigned integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void u4(std::uint32_t value) {
    write_unsigned(value);
  }
  /**
   * @brief Append a 8-byte unsigned integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void u8(std::uint64_t value) {
    write_unsigned(value);
  }
  /**
   * @brief Append a 1-byte signed integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void s1(std::int8_t value) {
    u1(std::bit_cast<std::uint8_t>(value));
  }
  /**
   * @brief Append a 2-byte signed integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void s2(std::int16_t value) {
    u2(std::bit_cast<std::uint16_t>(value));
  }
  /**
   * @brief Append a 4-byte signed integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void s4(std::int32_t value) {
    u4(std::bit_cast<std::uint32_t>(value));
  }
  /**
   * @brief Append a 8-byte signed integer in big-endian order.
   * @param value Integer value to encode in big-endian byte order.
   */
  void s8(std::int64_t value) {
    u8(std::bit_cast<std::uint64_t>(value));
  }

  /**
   * @brief Append a byte span to the caller-owned output container.
   * @param value Bytes copied during this call; must not alias the output container.
   */
  void bytes(std::span<const std::byte> value) {
    if (!value.empty())
      output.insert(output.end(), value.begin(), value.end());
  }

  /**
   * @brief Read the current output byte count.
   * @return The output container size in bytes.
   */
  [[nodiscard]] std::size_t size() const noexcept {
    return output.size();
  }

  /**
   * @brief Replace four previously emitted bytes with an unsigned big-endian value.
   * @param offset Byte offset into the supplied source/output range.
   * @param value Integer value to encode in big-endian byte order.
   * @return True if the complete patch fits in existing output; false without mutation otherwise.
   */
  [[nodiscard]] bool patch_u4(std::size_t offset, std::uint32_t value) noexcept {
    return patch_unsigned(offset, value);
  }
  /**
   * @brief Replace two previously emitted bytes with an unsigned big-endian value.
   * @param offset Byte offset into the supplied source/output range.
   * @param value Integer value to encode in big-endian byte order.
   * @return True if the complete patch fits in existing output; false without mutation otherwise.
   */
  [[nodiscard]] bool patch_u2(std::size_t offset, std::uint16_t value) noexcept {
    return patch_unsigned(offset, value);
  }

private:
  /**
   * @brief Replace a previously emitted fixed-width range with a big-endian value.
   * @param offset Byte offset into the supplied source/output range.
   * @param value Integer value to encode in big-endian byte order.
   * @return True if the complete patch fits in existing output; false without mutation otherwise.
   */
  template<typename Value>
  bool patch_unsigned(std::size_t offset, Value value) noexcept {
    if (offset > size() || sizeof(Value) > size() - offset)
      return false;
    for (std::size_t i = 0; i < sizeof(Value); ++i)
      output[offset + i] = static_cast<std::byte>((value >> ((sizeof(Value) - 1 - i) * 8)) & 0xffu);
    return true;
  }

  /**
   * @brief Append a fixed-width unsigned value in big-endian byte order.
   * @param value Integer value to encode in big-endian byte order.
   */
  template<typename Value>
  void write_unsigned(Value value) {
    for (std::size_t i = sizeof(Value); i > 0; --i)
      u1(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
  }

  Buffer& output; ///< Borrowed destination buffer; it must outlive this writer.
};

} // namespace cafe::detail
