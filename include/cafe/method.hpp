/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <initializer_list>
#include <memory_resource>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/bytecode/code_builder.hpp>
#include <cafe/bytecode/method_instruction.hpp>
#include <cafe/ids.hpp>

namespace cafe {

namespace pattern {
class Match;
}

namespace detail {
struct ClassStorage;

/// Sources that can supply an entire sequence for an editor operation.
/// @tparam T Range of instructions, a CodeBuilder, or a void callback receiving a builder.
template<typename T>
concept MethodSequence = std::same_as<std::remove_cvref_t<T>, CodeBuilder> ||
                         (std::ranges::input_range<T> &&
                          std::convertible_to<std::ranges::range_reference_t<T>, MethodInstruction>) ||
                         (std::invocable<T, CodeBuilder&> &&
                          std::same_as<std::invoke_result_t<T, CodeBuilder&>, void>);
} // namespace detail

/**
 * @brief Select which existing control-flow edges enter a before-insertion.
 * @ingroup bytecode
 */
enum class InsertionEntry {
  PreserveAnchor, ///< Existing branches and switches continue to target the original instruction.
  IncludeIncomingBranches, ///< Existing branch and switch edges enter at the first inserted instruction.
};

/**
 * @brief Method declaration owned by ClassFile. Code is either decoded, opaque, or absent. Adding
 * or erasing methods can invalidate references to this object.
 * @ingroup model
 */
class Method {
public:
  /**
   * @brief Move the declaration and transfer its attribute buffer. References to the source
   * declaration do not retarget to the destination.
   */
  Method(Method&&) noexcept = default;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Method& operator=(Method&&) = delete;

  /**
   * @brief Read the declaration's JVM access mask.
   * @return The declaration access bits.
   */
  [[nodiscard]] std::uint16_t access_flags() const noexcept {
    return flags;
  }
  /**
   * @brief Read the declaration name reference.
   * @return A UTF-8 constant-pool index owned by the containing class.
   */
  [[nodiscard]] CpIndex name_index() const noexcept {
    return name;
  }
  /**
   * @brief Read the declaration descriptor reference.
   * @return A UTF-8 constant-pool index containing the JVM descriptor.
   */
  [[nodiscard]] CpIndex descriptor_index() const noexcept {
    return descriptor;
  }
  /**
   * @brief Inspect ordered declaration attributes.
   * @return A borrowed span valid while this declaration remains alive and unchanged.
   */
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept {
    return attribute_entries;
  }
  /**
   * @brief Inspect decoded Code.
   * @return A borrowed pointer, or nullptr if Code is absent or opaque.
   */
  [[nodiscard]] const CodeAttribute* code() const noexcept;
  /**
   * @brief Inspect preserved undecodable Code.
   * @return A borrowed pointer, or nullptr if Code is absent or decoded.
   */
  [[nodiscard]] const OpaqueCode* opaque_code() const noexcept;

private:
  friend class ClassFile;
  friend class MethodEditor;
  friend class detail::ClassReader;

  /**
   * @brief Create an allocator-bound method declaration with constant-pool identity.
   * @param flags JVM access bit mask for the declaration.
   * @param name UTF-8 constant-pool reference for the name.
   * @param descriptor UTF-8 constant-pool reference for the descriptor.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  Method(std::uint16_t flags, CpIndex name, CpIndex descriptor, std::pmr::memory_resource* resource) : flags(flags), name(name), descriptor(descriptor), attribute_entries(resource) {
  }

  std::uint16_t flags; ///< JVM access bit mask.
  CpIndex name; ///< UTF-8 constant-pool reference for the declaration or attribute name.
  CpIndex descriptor; ///< UTF-8 constant-pool reference for the JVM descriptor.
  std::pmr::vector<Attribute> attribute_entries; ///< Owned ordered attribute list using the declaration's allocator.
};

/**
 * @brief Borrowed mutable access to one ClassFile method. Acquire a new editor after adding or
 * erasing a method; the owning class must outlive the editor.
 *
 * Symbolic edits validate operand structure and version rules. Frame analysis can check stack
 * usage, and the JVM resolves targets and invokes bootstrap methods at linkage. An allocation
 * failure may propagate as std::bad_alloc and leaves logical code and class entries unchanged.
 * Reacquire borrowed class attribute spans, bootstrap row views, and pool entry pointers after
 * symbolic edits.
 * @ingroup model
 */
class MethodEditor {
public:
  /// Ordered handles produced by one atomic sequence edit, or a failure with an optional element index.
  using SequenceResult = std::expected<std::vector<InstructionId>, BlockEditError>;

  /**
   * @brief Inspect the borrowed declaration.
   * @return The method reference; reacquire the editor after adding or erasing methods.
   */
  [[nodiscard]] const Method& method() const noexcept {
    return *member;
  }
  /**
   * @brief Access decoded Code for editing.
   * @return A borrowed mutable pointer, or nullptr when Code is absent or opaque.
   */
  [[nodiscard]] CodeAttribute* code() const noexcept;

  /**
   * @brief Append an instruction, resolving any symbolic operand against the owning class.
   * @param value Instruction and its operands to copy. Borrowed operands are needed only for this call.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> append(const MethodInstruction& value);
  /**
   * @brief Insert one instruction before a live anchor.
   * @param at Existing instruction handle in this method.
   * @param value Instruction and its operands to copy.
   * @param entry Policy for ordinary branch and switch edges targeting the anchor.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before(
      InstructionId at, const MethodInstruction& value, InsertionEntry entry = InsertionEntry::PreserveAnchor);
  /// Insert a single instruction immediately after a live anchor.
  /// @param at Live anchor in this method.
  /// @param value Instruction to validate and insert.
  /// @return The inserted handle or an edit error.
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_after(InstructionId at, const MethodInstruction& value);

  /// Insert a list atomically in supplied order. Exception handler labels retain their original bindings.
  /// @param at Live anchor in this method.
  /// @param values Instructions to validate and insert.
  /// @param entry Policy for existing branch and switch edges targeting the anchor.
  /// @return Inserted handles in order or an edit error.
  [[nodiscard]] SequenceResult insert_before(InstructionId at, std::initializer_list<MethodInstruction> values,
                                             InsertionEntry entry = InsertionEntry::PreserveAnchor) {
    return apply_sequence(at, false, entry, nullptr, values, nullptr);
  }
  /**
   * @brief Insert a range, standalone builder, or callback taking a CodeBuilder reference.
   * @tparam Sequence Supported instruction source.
   * @param at Live anchor in this method.
   * @param sequence Instructions or a void callback that emits them. Callback exceptions propagate.
   * @param entry Policy for ordinary branch and switch edges targeting the anchor.
   * @return Inserted IDs in order. An empty sequence succeeds at a live anchor.
   */
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_before(InstructionId at, Sequence&& sequence,
                                             InsertionEntry entry = InsertionEntry::PreserveAnchor) {
    return submit(at, false, entry, nullptr, std::forward<Sequence>(sequence));
  }
  /// Insert a list atomically after a live anchor, retaining input order.
  /// @param at Live anchor in this method.
  /// @param values Instructions to validate and insert.
  /// @return Inserted handles in order or an edit error.
  [[nodiscard]] SequenceResult insert_after(InstructionId at, std::initializer_list<MethodInstruction> values) {
    return apply_sequence(at, true, InsertionEntry::PreserveAnchor, nullptr, values, nullptr);
  }
  /// Insert a range, builder, or builder callback after a live anchor, retaining input order.
  /// @tparam Sequence Supported instruction source.
  /// @param at Live anchor in this method.
  /// @param sequence Source of the instructions to insert.
  /// @return Inserted handles in order or an edit error.
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_after(InstructionId at, Sequence&& sequence) {
    return submit(at, true, InsertionEntry::PreserveAnchor, nullptr, std::forward<Sequence>(sequence));
  }
  /**
   * @brief Insert at a bound label and move that label to the new instruction.
   * @param at Bound label in this method; the exclusive end is permitted.
   * @param value Instruction and its operands to copy.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_at(LabelId at, const MethodInstruction& value);
  /**
   * @brief Replace an instruction while retaining its handle and label bindings.
   * @param at Existing instruction handle in this method.
   * @param value Instruction and its operands to copy.
   * @return Success or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<void, CodeEditError> replace(InstructionId at, const MethodInstruction& value);
  /// Replace an unchanged contiguous match atomically. A nonempty replacement retains its entry ID and labels.
  /// @param match Saved match in this method.
  /// @param values Replacement instructions in method order.
  /// @return Handles of the replacement instructions or an edit error.
  [[nodiscard]] SequenceResult replace(const pattern::Match& match, std::initializer_list<MethodInstruction> values) {
    return apply_sequence({}, false, InsertionEntry::PreserveAnchor, &match, values, nullptr);
  }
  /// Replace a match with a range, builder, or builder callback. Check the match before running
  /// a builder callback or consuming an input range, then check it again after user code.
  /// @tparam Sequence Supported instruction source.
  /// @param match Saved match in this method.
  /// @param sequence Source of replacement instructions.
  /// @return Handles of the replacement instructions or an edit error.
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult replace(const pattern::Match& match, Sequence&& sequence) {
    return submit({}, false, InsertionEntry::PreserveAnchor, &match, std::forward<Sequence>(sequence));
  }
  /**
   * @brief Erase an instruction. Retarget any bound labels before erasing their destination.
   * @param id Existing instruction handle in this method.
   * @return Success or an edit error. Erasure leaves class constant and bootstrap indices stable.
   */
  [[nodiscard]] std::expected<void, CodeEditError> erase(InstructionId id);
  /// Atomically erase an unchanged contiguous match. Every removed instruction must be free of bound labels.
  /// @param match Saved match in this method.
  /// @return Success or an edit error.
  [[nodiscard]] std::expected<void, BlockEditError> erase(const pattern::Match& match) {
    return apply_sequence({}, false, InsertionEntry::PreserveAnchor, &match, {}, nullptr).transform([](auto&&) {});
  }

private:
  friend class ClassFile;
  /// Normalize sequence sources while keeping their operands live through validation and commit.
  /// @tparam Sequence Supported instruction source.
  /// @param at Insertion anchor when inserting by handle.
  /// @param after Select the boundary immediately after the anchor.
  /// @param entry Incoming branch policy for a before-insertion.
  /// @param match Saved range to replace, or null for insertion.
  /// @param source Instructions or callback to materialize.
  /// @return Handles produced by the edit or its validation error.
  template<detail::MethodSequence Sequence>
  SequenceResult submit(InstructionId at, bool after, InsertionEntry entry, const pattern::Match* match, Sequence&& source) {
    if constexpr (std::same_as<std::remove_cvref_t<Sequence>, CodeBuilder>) {
      return apply_sequence(at, after, entry, match, {}, &source);
    } else if constexpr (std::convertible_to<Sequence&, std::span<const MethodInstruction>>) {
      return apply_sequence(at, after, entry, match, source, nullptr);
    } else {
      if (match) {
        const auto start = match_position(*match);
        if (!start)
          return std::unexpected(start.error());
      }
      CodeBuilder builder;
      if constexpr (std::ranges::input_range<Sequence>) {
        for (auto&& instruction : source)
          builder.emit(std::forward<decltype(instruction)>(instruction));
      } else {
        std::invoke(std::forward<Sequence>(source), builder);
      }
      return apply_sequence(at, after, entry, match, {}, &builder);
    }
  }
  /// @param match Saved instruction identities and source revision.
  /// @return Validated current start ordinal or a stale match error.
  [[nodiscard]] std::expected<std::size_t, BlockEditError> match_position(const pattern::Match& match) const;
  /// Resolve operands and publish one sequence under a single class and method transaction.
  /// @param anchor Insertion anchor when inserting by handle.
  /// @param after Select the boundary immediately after the anchor.
  /// @param entry Incoming branch policy for a before-insertion.
  /// @param match Saved range to replace, or null for insertion.
  /// @param instructions Instructions supplied directly by the caller.
  /// @param builder Optional owner supplying instructions and label fixups.
  /// @return Handles produced by the edit or an indexed failure.
  SequenceResult apply_sequence(InstructionId anchor, bool after, InsertionEntry entry, const pattern::Match* match,
                                std::span<const MethodInstruction> instructions, const CodeBuilder* builder);
  /**
   * @brief Borrow mutable access to a live class-owned method.
   * @param storage Owning class storage.
   * @param method Method belonging to the supplied class.
   */
  MethodEditor(detail::ClassStorage& storage, Method& method) noexcept : storage(&storage), member(&method) {
  }

  detail::ClassStorage* storage; ///< Owning class storage, which stays fixed across ClassFile moves.
  Method* member; ///< Borrowed method; invalidated by adding or erasing methods in its class.
};

} // namespace cafe
