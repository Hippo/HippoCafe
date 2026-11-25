/**
 * @file
 * @brief Read a pending code sequence and its builder-local branch targets during an edit.
 * @ingroup implementation
 */
#pragma once

#include <cafe/bytecode/code.hpp>
#include <cafe/bytecode/code_builder.hpp>

namespace cafe::detail {

/// Borrows operands and fixups through an entire atomic publication.
struct BuilderAccess {
  /// Borrow the ordered source instructions. Builder-local branches have a placeholder operand.
  /// @param builder Pending instruction sequence.
  /// @return Instructions in emission order.
  static std::span<const MethodInstruction> instructions(const CodeBuilder& builder) noexcept;
  /// Borrow each builder label's optional boundary index, including the trailing boundary.
  /// @param builder Pending instruction sequence.
  /// @return Label bindings indexed by label ordinal.
  static std::span<const std::optional<std::size_t>> label_bindings(const CodeBuilder& builder) noexcept;
  /// @param builder Pending instruction sequence.
  /// @return Bound label ordinals in the order they were bound.
  static std::span<const std::size_t> binding_order(const CodeBuilder& builder) noexcept;
  /// Borrow (instruction index, label ordinal) pairs for builder-local branches and switches, with each switch default first.
  /// @param builder Pending instruction sequence.
  /// @return Instruction ordinals and their target label ordinals.
  static std::span<const std::pair<std::size_t, std::size_t>> branch_fixups(const CodeBuilder& builder) noexcept;
  /// Read the first sticky construction error, if present.
  /// @param builder Pending instruction sequence.
  /// @return Construction failure or nullopt after successful construction.
  static std::optional<BlockEditError> error(const CodeBuilder& builder) noexcept;
};

} // namespace cafe::detail
