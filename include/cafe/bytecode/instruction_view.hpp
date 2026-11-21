/**
 * @file
 * @brief Read-only instruction values with their owning class's symbolic context.
 * @ingroup bytecode
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>

#include <cafe/bytecode/code.hpp>
#include <cafe/bytecode/method_instruction.hpp>

namespace cafe {

namespace detail {
struct ClassStorage;
}

/**
 * @brief An instruction snapshot with its location and borrowed class context.
 *
 * The instruction and its handle are copied. Symbolic text borrows the class's
 * append-only pool. Keep the class storage alive, including after moving its
 * owner. A saved view describes the instruction as observed. Use its location
 * to look up the current instruction after editing. Erasing methods can shift
 * method indices, and erasing instructions invalidates their handles.
 * @ingroup bytecode
 */
class InstructionView {
public:
  /// @return The stable instruction handle belonging to the scanned method.
  [[nodiscard]] InstructionId id() const noexcept {
    return entry.id;
  }
  /// @return The method-table index at observation and the stable instruction handle.
  [[nodiscard]] InstructionLocation location() const noexcept {
    return {method_index, id()};
  }
  /// @return The normalized JVM opcode, independent of its original wire encoding.
  [[nodiscard]] Opcode opcode() const noexcept {
    return entry.instruction.opcode();
  }
  /**
   * @brief Compare the normalized JVM opcode.
   * @param value Logical opcode to compare.
   * @return True for an exact match.
   */
  [[nodiscard]] bool is(Opcode value) const noexcept {
    return opcode() == value;
  }
  /// @return The signed bipush or sipush operand, or nullopt for another opcode.
  [[nodiscard]] std::optional<std::int32_t> immediate() const {
    return entry.instruction.immediate_value();
  }
  /// @return The normalized local load/store/ret index, or nullopt for another opcode.
  [[nodiscard]] std::optional<std::uint16_t> local_index() const {
    return entry.instruction.local_index();
  }
  /// @return True for invokevirtual, invokespecial, invokestatic, or invokeinterface.
  [[nodiscard]] bool is_invoke() const noexcept {
    return opcode() >= Opcode::InvokeVirtual && opcode() <= Opcode::InvokeInterface;
  }
  /// @return True for getstatic, putstatic, getfield, or putfield.
  [[nodiscard]] bool is_field_access() const noexcept {
    return opcode() >= Opcode::GetStatic && opcode() <= Opcode::PutField;
  }
  /**
   * @brief Resolve an ordinary invocation's symbolic target.
   * @return A borrowed target, WrongOperandForm for a different opcode (including
   *   invokedynamic), or InvalidOperand for an invalid constant reference.
   */
  [[nodiscard]] std::expected<MethodInvocation, CodeEditError> method_invocation() const;
  /**
   * @brief Resolve a field instruction's symbolic target.
   * @return A borrowed target, WrongOperandForm for a different opcode,
   *   or InvalidOperand for an invalid constant reference.
   */
  [[nodiscard]] std::expected<FieldAccess, CodeEditError> field_access() const;
  /// @return A copy of the raw instruction, including its original encoding hint.
  [[nodiscard]] Instruction raw() const noexcept {
    return entry.instruction;
  }

private:
  friend class ClassFile;
  friend class InstructionViewIterator;

  /// Attach a copied instruction to the class storage supplying its symbolic operands.
  /// @param storage Move-stable class storage owning the constant pool.
  /// @param method_index Source method ordinal.
  /// @param entry Copied raw instruction and handle.
  InstructionView(const detail::ClassStorage* storage, std::size_t method_index, InstructionEntry entry) noexcept :
      storage(storage), method_index(method_index), entry(entry) {
  }

  const detail::ClassStorage* storage; ///< Borrowed class storage, stable across moves of the owner.
  std::size_t method_index; ///< Method-table position when the instruction was observed.
  InstructionEntry entry; ///< Copied raw instruction and its stable handle.
};

/**
 * @brief Forward iterator yielding class-aware instruction views by value.
 * @ingroup bytecode
 */
class InstructionViewIterator {
public:
  using iterator_concept = std::forward_iterator_tag; ///< Traversal supports multiple passes.
  using value_type = InstructionView; ///< Value produced by dereference.
  using difference_type = std::ptrdiff_t; ///< Signed distance in method order.

  /// Construct a singular iterator for an empty range.
  InstructionViewIterator() noexcept = default;
  /// Read the current instruction and its class context. Requires a valid non-end iterator.
  /// @return The instruction view at the current position.
  [[nodiscard]] InstructionView operator*() const noexcept {
    return {storage, method_index, *current};
  }
  /// Advance by one instruction in method order.
  /// @return This iterator after advancing.
  InstructionViewIterator& operator++() noexcept {
    ++current;
    return *this;
  }
  /// Advance by one instruction and return the previous position.
  /// @return An iterator to the previous position.
  InstructionViewIterator operator++(int) noexcept {
    auto previous = *this;
    ++*this;
    return previous;
  }
  friend bool operator==(const InstructionViewIterator&, const InstructionViewIterator&) = default;

private:
  friend class ClassFile;
  /// Wrap an existing method-order iterator with class resolution context.
  /// @param storage Move-stable class storage owning the constant pool.
  /// @param method_index Source method ordinal.
  /// @param current Position in the method code.
  InstructionViewIterator(const detail::ClassStorage* storage, std::size_t method_index, MethodCode::InstructionIterator current) noexcept :
      storage(storage), method_index(method_index), current(current) {
  }

  const detail::ClassStorage* storage = nullptr; ///< Owner context for symbol resolution.
  std::size_t method_index = 0; ///< Method-table position at traversal start.
  MethodCode::InstructionIterator current; ///< Position in the low-level instruction sequence.
};

/**
 * @brief Sized borrowed forward range of read-only instruction views in method order.
 * Reacquire the range after editing the method or changing the class's method table.
 * @ingroup bytecode
 */
using InstructionViewRange = std::ranges::subrange<InstructionViewIterator, InstructionViewIterator, std::ranges::subrange_kind::sized>;

/**
 * @brief Format a logical mnemonic with its immediate, local, or constant operand when present.
 * @param instruction Class-aware instruction to render.
 * @return An owned diagnostic string. An unresolved member uses its constant-pool index.
 * @ingroup bytecode
 */
[[nodiscard]] std::string format(InstructionView instruction);

} // namespace cafe
