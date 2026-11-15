#pragma once

#include <cstdint>

namespace cafe {

struct CpIndex {
  std::uint16_t value = 0;
  explicit constexpr operator bool() const noexcept {
    return value != 0;
  }
  friend constexpr bool operator==(CpIndex, CpIndex) = default;
};

struct InstructionId {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
  friend constexpr bool operator==(InstructionId, InstructionId) = default;
};

struct LabelId {
  std::uint32_t value = 0;
  std::uint32_t scope = 0;
  friend constexpr bool operator==(LabelId, LabelId) = default;
};

struct SwitchId {
  std::uint32_t value = 0;
  std::uint32_t scope = 0;
  friend constexpr bool operator==(SwitchId, SwitchId) = default;
};

} // namespace cafe
