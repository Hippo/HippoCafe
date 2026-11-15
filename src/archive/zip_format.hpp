#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cafe::archive::detail {

inline constexpr std::uint32_t EOCD_MAGIC = 0x06054b50;
inline constexpr std::uint32_t ZIP64_EOCD_MAGIC = 0x06064b50;
inline constexpr std::uint32_t ZIP64_LOCATOR_MAGIC = 0x07064b50;
inline constexpr std::uint32_t LOCAL_HEADER_MAGIC = 0x04034b50;
inline constexpr std::uint32_t CENTRAL_HEADER_MAGIC = 0x02014b50;
inline constexpr std::array JMOD_MAGIC{std::byte{'J'}, std::byte{'M'}, std::byte{1}, std::byte{0}};
inline constexpr std::size_t LOCAL_HEADER_SIZE = 30;
inline constexpr std::size_t CENTRAL_HEADER_SIZE = 46;
inline constexpr std::size_t EOCD_SIZE = 22;

template<typename Uint>
Uint little_endian(std::span<const std::byte> bytes, std::size_t at) {
  Uint value = 0;
  for (std::size_t i = sizeof(Uint); i > 0; --i)
    value = static_cast<Uint>((value << 8) | std::to_integer<unsigned>(bytes[at + i - 1]));
  return value;
}

[[nodiscard]] bool safe_entry_name(std::string_view name, std::size_t max_depth);

} // namespace cafe::archive::detail
