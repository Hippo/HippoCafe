/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup implementation
 */

#pragma once

#include <span>

#include "cafe/bytecode/code.hpp"

namespace cafe::detail {

/**
 * @brief Restricted codec access to method storage identity and original-wire metadata. These
 * helpers must not be used as public edit operations.
 * @ingroup implementation
 */
struct BytecodeAccess {
  /**
   * @brief Copy an instruction while setting its original wire-encoding preference.
   * @param value Instruction copied with the supplied encoding hint.
   * @param hint Preferred wire-encoding form.
   * @return The adjusted compact instruction.
   */
  static Instruction with_hint(Instruction value, EncodingHint hint) {
    value.hint = hint;
    return value;
  }

  /**
   * @brief Read the owning method's label/switch identity scope.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @return The nonzero method scope, or zero on handle-generation exhaustion.
   */
  static std::uint32_t scope(const MethodCode& code);
  /**
   * @brief Count allocated instruction slots, including erased/free entries.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @return The slot-table size used for bounded indexing choices.
   */
  static std::size_t slot_count(const MethodCode& code);
  /**
   * @brief Count allocated label entries.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @return The label-table size.
   */
  static std::size_t label_count(const MethodCode& code);

  /**
   * @brief Create an original-wire label without recording a user edit.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param position Instruction-order position or explicit code boundary.
   * @return A scoped label or a handle-limit error.
   *
   * Reader-only helpers to attach original metadata boundaries without
   * recording a user edit.
   */
  static std::expected<LabelId, CodeEditError> original_label(MethodCode& code, CodePosition position);
  /**
   * @brief Reset the instruction revision after successful initial decoding.
   * @param code Decoded instruction sequence or associated Code attribute.
   */
  static void mark_clean(MethodCode& code);

  /**
   * @brief Retain original permitted switch padding bytes without recording a user edit.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param id Stable instruction handle belonging to the supplied method.
   * @param padding Original switch alignment bytes to retain.
   */
  static void set_original_switch_padding(MethodCode& code, InstructionId id, std::span<const std::byte> padding);
  /**
   * @brief Inspect retained original switch padding.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return A borrowed padding byte span associated with the switch operand.
   */
  static std::span<const std::byte> original_switch_padding(const MethodCode& code, InstructionId id);
};

} // namespace cafe::detail
