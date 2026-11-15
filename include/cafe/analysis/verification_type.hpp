#pragma once

#include <cstdint>
#include <string_view>

#include "cafe/ids.hpp"

namespace cafe::analysis {

enum class VerificationKind : std::uint8_t {
  Top,
  Integer,
  Float,
  Long,
  Double,
  Null,
  UninitializedThis,
  Object,
  Uninitialized,
  WideContinuation,
};

struct VerificationType {
  VerificationKind kind = VerificationKind::Top;
  // For Object, the JVM internal name or array descriptor. For Uninitialized,
  // the name of the allocated class and the stable ID of the new instruction.
  // These views borrow their owning analysis result.
  std::string_view reference{};
  InstructionId allocation{};

  [[nodiscard]] constexpr unsigned width() const noexcept {
    return kind == VerificationKind::Long || kind == VerificationKind::Double ? 2 : 1;
  }

  friend constexpr bool operator==(VerificationType, VerificationType) = default;
};

} // namespace cafe::analysis
