#pragma once

#include <cstdint>

namespace cafe {

inline constexpr std::uint32_t CLASS_MAGIC = 0xcafebabe;
inline constexpr std::uint32_t MAX_CODE_SIZE = 65535;

struct ClassVersion {
  std::uint16_t major = 45;
  std::uint16_t minor = 3;
  friend constexpr bool operator==(ClassVersion, ClassVersion) = default;

  [[nodiscard]] constexpr bool has_legal_minor() const noexcept {
    return major < 56 || minor == 0 || minor == 65535;
  }
};

} // namespace cafe
