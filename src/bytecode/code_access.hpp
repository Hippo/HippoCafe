/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup implementation
 */

#pragma once

#include <span>

#include "cafe/bytecode/code.hpp"

namespace cafe::detail {

/// Placement of an atomic class-aware instruction patch.
enum class PatchPlacement {
  Before, ///< Insert before the anchor.
  After, ///< Insert after the anchor.
  Replace, ///< Replace the anchor or checked match.
  Append, ///< Insert at method end.
};

/// The original edit location, match identity and unresolved builder-local branches.
struct CodePatch {
  PatchPlacement placement = PatchPlacement::Before; ///< Operation relative to the anchor or match.
  InstructionId anchor{}; ///< Anchor for insertion or replacement by ID.
  std::span<const InstructionId> matched_ids{}; ///< Exact ordered IDs of a range replacement.
  std::optional<std::size_t> match_position{}; ///< Validated start ordinal of a match in the current method.
  bool include_incoming{}; ///< Redirect existing branches and switches entering the anchor.
  std::span<const std::optional<std::size_t>> label_bindings{}; ///< Builder label to boundary ordinal.
  std::span<const std::pair<std::size_t, std::size_t>> branch_fixups{}; ///< Ordered (instruction index, label ordinal) pairs, with each switch default preceding its cases.
  std::optional<LabelId> move_label{}; ///< Selected existing insert_at boundary to bind to the patch entry.
  std::optional<LabelId> label_anchor{}; ///< Insert on either side of this node at a shared boundary.
  bool ordered{}; ///< Use node order, including preceding labels for a before-instruction edit.
  std::span<const std::size_t> binding_order{}; ///< Builder label ordinals in binding order.
};

/**
 * @brief Restricted codec access to method storage identity and original-wire metadata. These
 * helpers must not be used as public edit operations.
 * @ingroup implementation
 */
struct BytecodeAccess {
  /// Find a saved match in current instruction order and verify its identity and operand stamps.
  /// @param code Live method receiving the edit.
  /// @param ids Stable identities, in original match order.
  /// @param scope Source method scope.
  /// @param revision Source revision when the match was observed.
  /// @return First current ordinal, or StaleMatch for an invalidated, empty or foreign match.
  static std::expected<std::size_t, BlockEditError> match_position(const MethodCode& code,
                                                                   std::span<const InstructionId> ids,
                                                                   std::uint32_t scope, std::uint64_t revision);
  /// Apply an atomic splice after symbolic operands have been resolved.
  /// @param code Live method to edit.
  /// @param patch Anchor, optional match validation, and label fixups.
  /// @param block Concrete instructions and method-scoped operands in insertion order.
  /// @return Published IDs or an error leaving the logical method unchanged.
  static std::expected<std::vector<InstructionId>, BlockEditError> apply_patch(
      MethodCode& code, const CodePatch& patch, std::span<const InstructionBuilder> block);
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
  /// Bind a label during construction of an unpublished decoded or lowered body.
  /// @param code Detached method whose instruction identities have not yet been published.
  /// @param label Method-scoped label to bind.
  /// @param position Instruction boundary, including the exclusive end.
  /// @return Success or an invalid label, instruction, or revision error.
  static std::expected<void, CodeEditError> bind_initial_label(MethodCode& code, LabelId label, CodePosition position) {
    return code.set_label_binding(label, position, false);
  }
  /**
   * @brief Reset the instruction revision after successful initial decoding.
   * @param code Decoded instruction sequence or associated Code attribute.
   */
  static void mark_clean(MethodCode& code);

  /// Publish a fully staged instruction owner while preserving the target object's address.
  /// @param target Canonical instruction owner receiving the new scope.
  /// @param staged Detached instruction owner; receives the previous storage.
  static void swap_storage(MethodCode& target, MethodCode& staged) noexcept {
    target.storage.swap(staged.storage);
  }

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
