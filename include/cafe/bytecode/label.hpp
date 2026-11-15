/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <optional>

#include "cafe/ids.hpp"

namespace cafe {

/**
 * @brief Boundary immediately before an instruction, or the exclusive method end when before is
 * empty. This differs from an unbound label.
 * @ingroup bytecode
 */
struct CodePosition {
  std::optional<InstructionId> before; ///< Instruction immediately after this boundary; empty denotes the exclusive method end.

  friend bool operator==(const CodePosition&, const CodePosition&) = default;
};

} // namespace cafe
