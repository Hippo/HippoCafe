/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "cafe/ids.hpp"

namespace cafe::analysis {

/**
 * @brief Verification value categories and internal wide-local continuation marker.
 * @ingroup analysis
 */
enum class VerificationKind : std::uint8_t {
  Top, ///< Unusable or unknown verification slot.
  Integer, ///< JVM int verification value, also used for byte/char/short/boolean computation.
  Float, ///< JVM float value.
  Long, ///< JVM long value occupying two slots.
  Double, ///< JVM double value occupying two slots.
  Null, ///< Null reference assignable to reference types.
  UninitializedThis, ///< Receiver of an instance constructor before initialization.
  Object, ///< Initialized reference with an internal name or array descriptor.
  Uninitialized, ///< Object identity associated with a particular new instruction.
  WideContinuation, ///< Internal second slot following a long/double local.
};

/**
 * @brief JVM verification value, including uninitialized allocation identities. Reference names
 * borrow their owning analysis result; WideContinuation is an internal local-slot marker.
 * @ingroup analysis
 */
struct VerificationType {
  VerificationKind kind = VerificationKind::Top; ///< Discriminator selecting the interpretation of this record.
  /**
   * @brief Borrowed internal class name or array descriptor; meaningful for reference and
   * uninitialized values.
   *
   * For Object, the JVM internal name or array descriptor. For Uninitialized,
   * the name of the allocated class and the stable ID of the new instruction.
   * These views borrow their owning analysis result.
   */
  std::string_view reference{};
  InstructionId allocation{}; ///< Stable identity of the new instruction for an uninitialized value.

  /**
   * @brief Count JVM slots occupied by this verification value.
   * @return Two for long/double and one for every other kind, including the continuation marker.
   */
  [[nodiscard]] constexpr unsigned width() const noexcept {
    return kind == VerificationKind::Long || kind == VerificationKind::Double ? 2 : 1;
  }

  friend constexpr bool operator==(VerificationType, VerificationType) = default;
};

} // namespace cafe::analysis
