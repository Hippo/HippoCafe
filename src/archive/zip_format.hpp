/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cafe::archive::detail {

inline constexpr std::uint32_t EOCD_MAGIC = 0x06054b50; ///< Classic ZIP end-of-central-directory signature.
inline constexpr std::uint32_t ZIP64_EOCD_MAGIC = 0x06064b50; ///< ZIP64 end-of-central-directory record signature.
inline constexpr std::uint32_t ZIP64_LOCATOR_MAGIC = 0x07064b50; ///< ZIP64 end-of-central-directory locator signature.
inline constexpr std::uint32_t LOCAL_HEADER_MAGIC = 0x04034b50; ///< ZIP local-file-header signature.
inline constexpr std::uint32_t CENTRAL_HEADER_MAGIC = 0x02014b50; ///< ZIP central-directory-header signature.
inline constexpr std::array JMOD_MAGIC{std::byte{'J'}, std::byte{'M'}, std::byte{1}, std::byte{0}}; ///< Supported JMOD wrapper magic and version bytes.
inline constexpr std::size_t LOCAL_HEADER_SIZE = 30; ///< Fixed ZIP local-header width before variable fields.
inline constexpr std::size_t CENTRAL_HEADER_SIZE = 46; ///< Fixed ZIP central-header width before variable fields.
inline constexpr std::size_t EOCD_SIZE = 22; ///< Fixed classic end-of-directory width before the archive comment.

/**
 * @brief Read an unsigned little-endian value from a previously checked byte range.
 * @param bytes Borrowed byte range containing the complete integer at the requested offset.
 * @param at Byte offset of the little-endian value within the supplied buffer.
 * @return The decoded unsigned value; caller guarantees enough bytes at the offset.
 */
template<typename Uint>
Uint little_endian(std::span<const std::byte> bytes, std::size_t at) {
  Uint value = 0;
  for (std::size_t i = sizeof(Uint); i > 0; --i)
    value = static_cast<Uint>((value << 8) | std::to_integer<unsigned>(bytes[at + i - 1]));
  return value;
}

/**
 * @brief Reject absolute, traversal, empty-component, and over-depth archive paths.
 * @param name Case-sensitive archive entry name with slash separators.
 * @param max_depth Maximum path-component depth.
 * @return True for a safe relative slash-separated name.
 */
[[nodiscard]] bool safe_entry_name(std::string_view name, std::size_t max_depth);

} // namespace cafe::archive::detail
