/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>

namespace cafe {

inline constexpr std::uint32_t CLASS_MAGIC = 0xcafebabe; ///< Required class-file magic value, CAFEBABE.
inline constexpr std::uint32_t MAX_CODE_SIZE = 65535; ///< Maximum JVM Code byte length, 65535.

/**
 * @brief Exact class-file major/minor version, including preview minor versions. The default is
 * the standard 45.3 format.
 * @ingroup model
 */
struct ClassVersion {
  std::uint16_t major = 45; ///< Class-file major version, defaulting to 45.
  std::uint16_t minor = 3; ///< Class-file minor version, defaulting to 3; 65535 denotes preview use for modern versions.
  friend constexpr bool operator==(ClassVersion, ClassVersion) = default;

  /**
   * @brief Check the standard minor-version rule.
   * @return True for majors below 56 or for minor zero/65535; this does not check the major-version
   *   support profile.
   */
  [[nodiscard]] constexpr bool has_legal_minor() const noexcept {
    return major < 56 || minor == 0 || minor == 65535;
  }
};

} // namespace cafe
