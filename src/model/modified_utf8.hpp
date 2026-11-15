#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cafe::detail {

inline bool next_modified_utf8(std::string_view bytes, std::size_t& cursor, char16_t& value) noexcept {
  const auto first = static_cast<std::uint8_t>(bytes[cursor++]);
  if (first != 0 && first < 0x80) {
    value = first;
    return true;
  }
  if (first < 0xc0 || first >= 0xf0 || cursor == bytes.size())
    return false;
  const auto second = static_cast<std::uint8_t>(bytes[cursor++]);
  if ((second & 0xc0) != 0x80)
    return false;
  if (first < 0xe0) {
    if ((first == 0xc0 && second != 0x80) || first == 0xc1)
      return false;
    value = static_cast<char16_t>(((first & 0x1f) << 6) | (second & 0x3f));
    return true;
  }
  if ((first == 0xe0 && second < 0xa0) || cursor == bytes.size())
    return false;
  const auto third = static_cast<std::uint8_t>(bytes[cursor++]);
  if ((third & 0xc0) != 0x80)
    return false;
  value = static_cast<char16_t>(((first & 0x0f) << 12) | ((second & 0x3f) << 6) | (third & 0x3f));
  return true;
}

inline bool valid_modified_utf8(std::string_view bytes) noexcept {
  std::size_t cursor = 0;
  char16_t unit = 0;
  while (cursor < bytes.size())
    if (!next_modified_utf8(bytes, cursor, unit))
      return false;
  return true;
}

} // namespace cafe::detail
