/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <cstdint>
#include <span>

#include "cafe/ids.hpp"

namespace cafe {

/**
 * @brief Borrowed dense switch descriptor with targets for consecutive keys starting at low.
 * MethodCode copies the targets during insertion.
 * @ingroup bytecode
 */
struct TableSwitch {
  std::int32_t low; ///< First signed dense-switch key; targets correspond to consecutive keys.
  LabelId default_target; ///< Destination used when no case key matches.
  std::span<const LabelId> targets; ///< Borrowed dense-switch labels; copied on insertion.
};

/**
 * @brief Signed lookup-switch key and its destination label.
 * @ingroup bytecode
 */
struct LookupPair {
  std::int32_t key; ///< Signed sparse-switch case key.
  LabelId target; ///< Method-scoped destination for this signed lookup key.

  friend bool operator==(const LookupPair&, const LookupPair&) = default;
};

/**
 * @brief Borrowed sparse switch descriptor.
 *
 * Keys must be strictly increasing; MethodCode copies the pairs during insertion.
 * @ingroup bytecode
 */
struct LookupSwitch {
  LabelId default_target; ///< Destination used when no case key matches.
  std::span<const LookupPair> pairs; ///< Borrowed strictly increasing sparse-switch pairs; copied on insertion.
};

} // namespace cafe
