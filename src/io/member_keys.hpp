#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>

namespace cafe::detail {

struct MemberKey {
  std::string_view name;
  std::string_view descriptor;
  std::size_t index;
  std::size_t offset;
};

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
