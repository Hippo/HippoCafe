/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>

namespace cafe {

/**
 * @brief One-based constant-pool slot index. Zero represents absence where the enclosing format
 * permits it.
 * @ingroup model
 */
struct CpIndex {
  std::uint16_t value = 0; ///< Wire slot index; zero denotes absence where allowed.
  /**
   * @brief Test whether this index denotes a nonzero slot.
   * @return True for nonzero; pool membership and tag validity are not checked.
   */
  explicit constexpr operator bool() const noexcept {
    return value != 0;
  }
  friend constexpr bool operator==(CpIndex, CpIndex) = default;
};

/**
 * @brief Generation-checked instruction handle scoped to its owning MethodCode. Erasure
 * invalidates the handle; unrelated edits preserve it. The zero generation is invalid.
 * @ingroup model
 */
struct InstructionId {
  std::uint32_t index = 0; ///< Instruction slot index; use generation to reject stale or foreign handles.
  std::uint32_t generation = 0; ///< Nonzero handle generation; changes when a slot is reused.
  friend constexpr bool operator==(InstructionId, InstructionId) = default;
};

/**
 * @brief Method-scoped label identifying an instruction boundary, including the exclusive end.
 *
 * Default construction yields an invalid handle.
 * @ingroup model
 */
struct LabelId {
  std::uint32_t value = 0; ///< Method-local label-table index.
  std::uint32_t scope = 0; ///< Method identity used to reject foreign labels or switches.
  friend constexpr bool operator==(LabelId, LabelId) = default;
};

/**
 * @brief Method-scoped switch operand handle with generation information packed into its value.
 *
 * Default construction yields an invalid handle.
 * @ingroup model
 */
struct SwitchId {
  std::uint32_t value = 0; ///< Packed switch slot and generation.
  std::uint32_t scope = 0; ///< Method identity used to reject foreign labels or switches.
  friend constexpr bool operator==(SwitchId, SwitchId) = default;
};

} // namespace cafe
