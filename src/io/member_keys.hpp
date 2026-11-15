/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>

namespace cafe::detail {

/**
 * @brief Borrowed name/descriptor pair with its original table index for duplicate detection.
 * @ingroup implementation
 */
struct MemberKey {
  std::string_view name; ///< JVM internal or declaration name in modified UTF-8.
  std::string_view descriptor; ///< JVM field or method descriptor.
  std::size_t index; ///< Original declaration-table index retained while sorting duplicate keys.
  std::size_t offset; ///< Byte offset relative to the owning source or code buffer.
};

/**
 * @brief Sort member identity keys and identify an exact duplicate name/descriptor.
 * @param members Field or method declarations in the required order.
 * @return The duplicate's table ordinal when present, otherwise an empty optional.
 * @ingroup implementation
 */
[[nodiscard]] inline const MemberKey* find_duplicate_member(std::span<MemberKey> members) {
  if (members.size() < 2)
    return nullptr;
  std::ranges::sort(members, {}, [](const MemberKey& member) {
    return std::tie(member.name, member.descriptor, member.index);
  });
  const auto duplicate = std::ranges::adjacent_find(members, [](const MemberKey& first, const MemberKey& second) {
    return first.name == second.name && first.descriptor == second.descriptor;
  });
  return duplicate == members.end() ? nullptr : &*(duplicate + 1);
}

} // namespace cafe::detail
