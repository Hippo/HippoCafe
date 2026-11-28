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
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/bytecode/code_builder.hpp>
#include <cafe/bytecode/method_instruction.hpp>
#include <cafe/bytecode/pattern.hpp>
#include <cafe/error.hpp>
#include <cafe/ids.hpp>

namespace cafe {

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

/// Explicit ordered-sequence handles; a bare InstructionId uses the compatible overloads.
/// @tparam T Caller's handle type.
template<typename T>
concept SequenceNode = std::same_as<T, CodeNode> || std::same_as<T, LabelId>;
} // namespace detail

/**
 * @brief Select the branch behavior of the compatible instruction-ID insertion overloads.
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
  friend class detail::ClassPoolCompaction;
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
 * Symbolic instruction and exception-handler edits retry once after pool compaction if they
 * exhaust the model's constant slots. A successful retry can renumber pool indices and
 * invalidate borrowed constant data. Instruction handles and labels survive compaction.
 *
 * Symbolic edits validate operand structure and version rules. Frame analysis can check stack
 * usage, and the JVM resolves targets and invokes bootstrap methods at linkage. An allocation
 * failure may propagate as std::bad_alloc and leaves logical code and class entries unchanged.
 * Reacquire borrowed class attribute spans, bootstrap row views, and pool entry pointers after
 * symbolic edits.
 * @ingroup model
 */
class MethodEditor {
  /// Select a splice location and its effect on existing labels and jumps.
  enum class Placement {
    Before, ///< Insert ahead of an instruction and its labels.
    After, ///< Insert after the specified node.
    BeforeNode, ///< Insert before the specified node in sequence order.
    BranchEntry, ///< Insert ahead of an instruction's labels and redirect jumps and switches.
    Replace, ///< Replace a checked pattern match.
  };

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
  /// Create an empty Code attribute for a concrete method.
  /// @return The new body or a declaration error.
  [[nodiscard]] std::expected<std::reference_wrapper<CodeAttribute>, ModelError> add_code();

  /// Read decoded instructions in method order.
  /// @return A borrowed range, empty for absent or opaque Code.
  [[nodiscard]] InstructionViewRange instructions() const noexcept;
  /// Inspect an instruction obtained from the editable sequence or a saved match.
  /// @param id Live instruction handle in this method.
  /// @return A class-aware view or an edit error.
  [[nodiscard]] std::expected<InstructionView, CodeEditError> instruction(InstructionId id) const noexcept;
  /// Get the editable sequence in order, including bound labels.
  /// @return A snapshot of handles, empty for absent or opaque Code.
  [[nodiscard]] std::vector<CodeNode> nodes() const;
  /// Create a bound label immediately before an instruction, after any labels already there.
  /// @param instruction Live instruction handle in this method.
  /// @return The new label or an edit error.
  [[nodiscard]] std::expected<LabelId, CodeEditError> label_before(InstructionId instruction);
  /// Create a bound label at the exclusive end of the method.
  /// @return The new label or an edit error.
  [[nodiscard]] std::expected<LabelId, CodeEditError> label_at_end();
  /// Append an exception handler using a symbolic catch type.
  /// @param start Inclusive start label of the protected range.
  /// @param end Exclusive end label of the protected range.
  /// @param handler Label at the first executable instruction of the handler.
  /// @param catch_type JVM internal class name in modified UTF-8, or nullopt for a catch-all.
  /// @return Success or an error preserving the exception table and constant pool.
  [[nodiscard]] std::expected<void, CodeEditError> add_exception_handler(
      LabelId start, LabelId end, LabelId handler, std::optional<std::string_view> catch_type = std::nullopt);
  /// Search this method for the first consuming match.
  /// @param query Instruction pattern to run.
  /// @param options Search work limit.
  /// @return The first match, an empty optional, or a search error.
  [[nodiscard]] std::expected<std::optional<pattern::Match>, pattern::MatchError> find_first(
      const pattern::Pattern& query, pattern::MatchOptions options = {}) const;
  /// Search this method in instruction order.
  /// @param query Instruction pattern to run.
  /// @param options Overlap and work settings.
  /// @return Matches or a search error.
  [[nodiscard]] std::expected<std::vector<pattern::Match>, pattern::MatchError> find_all(
      const pattern::Pattern& query, pattern::MatchOptions options = {}) const;

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
    return apply_sequence(at, legacy_placement(entry), nullptr, values, nullptr);
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
    return submit(at, legacy_placement(entry), nullptr, std::forward<Sequence>(sequence));
  }
  /// Insert a list atomically after a live anchor, retaining input order.
  /// @param at Live anchor in this method.
  /// @param values Instructions to validate and insert.
  /// @return Inserted handles in order or an edit error.
  [[nodiscard]] SequenceResult insert_after(InstructionId at, std::initializer_list<MethodInstruction> values) {
    return apply_sequence(at, Placement::After, nullptr, values, nullptr);
  }
  /// Insert a range, builder, or builder callback after a live anchor, retaining input order.
  /// @tparam Sequence Supported instruction source.
  /// @param at Live anchor in this method.
  /// @param sequence Source of the instructions to insert.
  /// @return Inserted handles in order or an edit error.
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_after(InstructionId at, Sequence&& sequence) {
    return submit(at, Placement::After, nullptr, std::forward<Sequence>(sequence));
  }

  /// Insert immediately before a sequence node. Earlier labels lead into the insertion.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @param at Node from nodes(), a LabelId, or an InstructionId explicitly wrapped in CodeNode.
  /// @param value Instruction to insert.
  /// @return Inserted handle or an edit error.
  template<detail::SequenceNode Node>
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before(Node at, const MethodInstruction& value) {
    return insert_node(at, Placement::BeforeNode, value);
  }
  /// Insert immediately after a sequence node, before any subsequent labels.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @param at Node from nodes(), a LabelId, or an InstructionId wrapped in CodeNode.
  /// @param value Instruction to insert.
  /// @return Inserted handle or an edit error.
  template<detail::SequenceNode Node>
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_after(Node at, const MethodInstruction& value) {
    return insert_node(at, Placement::After, value);
  }
  /// Insert a list immediately before a sequence node.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @param at Selected instruction or label node.
  /// @param values Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  template<detail::SequenceNode Node>
  [[nodiscard]] SequenceResult insert_before(Node at, std::initializer_list<MethodInstruction> values) {
    return apply_sequence(at, Placement::BeforeNode, nullptr, values, nullptr);
  }
  /// Insert a list immediately after a sequence node.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @param at Selected instruction or label node.
  /// @param values Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  template<detail::SequenceNode Node>
  [[nodiscard]] SequenceResult insert_after(Node at, std::initializer_list<MethodInstruction> values) {
    return apply_sequence(at, Placement::After, nullptr, values, nullptr);
  }
  /// Insert a range, builder, or builder callback immediately before a sequence node.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @tparam Sequence Supported instruction source.
  /// @param at Selected instruction or label node.
  /// @param sequence Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  template<detail::SequenceNode Node, detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_before(Node at, Sequence&& sequence) {
    return submit(at, Placement::BeforeNode, nullptr, std::forward<Sequence>(sequence));
  }
  /// Insert a range, builder, or builder callback immediately after a sequence node.
  /// @tparam Node Explicit ordered-sequence handle type.
  /// @tparam Sequence Supported instruction source.
  /// @param at Selected instruction or label node.
  /// @param sequence Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  template<detail::SequenceNode Node, detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_after(Node at, Sequence&& sequence) {
    return submit(at, Placement::After, nullptr, std::forward<Sequence>(sequence));
  }

  /// Insert before an instruction and redirect existing jumps and switches through the insertion.
  /// @param at Live instruction. Exception boundaries keep their existing destinations.
  /// @param value Instruction to insert.
  /// @return Inserted handle or an edit error.
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before_and_redirect_branches(
      InstructionId at, const MethodInstruction& value) {
    return insert_before(at, value, InsertionEntry::IncludeIncomingBranches);
  }
  /// Insert a list and redirect existing jumps and switches through it.
  /// @param at Live instruction. Exception boundaries keep their existing destinations.
  /// @param values Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  [[nodiscard]] SequenceResult insert_before_and_redirect_branches(
      InstructionId at, std::initializer_list<MethodInstruction> values) {
    return apply_sequence(at, Placement::BranchEntry, nullptr, values, nullptr);
  }
  /// Insert a sequence and redirect existing jumps and switches through it.
  /// @tparam Sequence Supported instruction source.
  /// @param at Live instruction. Exception boundaries keep their existing destinations.
  /// @param sequence Instructions to insert atomically.
  /// @return Inserted handles or an edit error.
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult insert_before_and_redirect_branches(InstructionId at, Sequence&& sequence) {
    return submit(at, Placement::BranchEntry, nullptr, std::forward<Sequence>(sequence));
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
    return apply_sequence(InstructionId{}, Placement::Replace, &match, values, nullptr);
  }
  /// Replace a match with a range, builder, or builder callback. Check the match before running
  /// a builder callback or consuming an input range, then check it again after user code.
  /// @tparam Sequence Supported instruction source.
  /// @param match Saved match in this method.
  /// @param sequence Source of replacement instructions.
  /// @return Handles of the replacement instructions or an edit error.
  template<detail::MethodSequence Sequence>
  [[nodiscard]] SequenceResult replace(const pattern::Match& match, Sequence&& sequence) {
    return submit(InstructionId{}, Placement::Replace, &match, std::forward<Sequence>(sequence));
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
    return apply_sequence(InstructionId{}, Placement::Replace, &match, {}, nullptr).transform([](auto&&) {});
  }

private:
  friend class ClassFile;
  /// @return Current class method-table index while the editor is valid.
  [[nodiscard]] std::size_t method_index() const noexcept;
  /// @param at Selected instruction or label.
  /// @param placement Splice location relative to the handle.
  /// @return The internal edit descriptor.
  static detail::CodePatch code_patch(CodeNode at, Placement placement);
  /// @param at Selected instruction or label.
  /// @param placement Splice location relative to the handle.
  /// @param value Single instruction to emit.
  /// @return The new instruction ID or an edit error.
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_node(
      CodeNode at, Placement placement, const MethodInstruction& value);
  /// @param entry Branch policy from the compatible instruction-ID overload.
  /// @return The corresponding edit placement.
  static constexpr Placement legacy_placement(InsertionEntry entry) noexcept {
    return entry == InsertionEntry::IncludeIncomingBranches ? Placement::BranchEntry : Placement::Before;
  }
  /// Normalize sequence sources while keeping their operands live through validation and commit.
  /// @tparam Sequence Supported instruction source.
  /// @param at Insertion anchor when inserting by handle.
  /// @param placement Edit position relative to the supplied handle.
  /// @param match Saved range to replace, or null for insertion.
  /// @param source Instructions or callback to materialize.
  /// @return Handles produced by the edit or its validation error.
  template<detail::MethodSequence Sequence>
  SequenceResult submit(CodeNode at, Placement placement, const pattern::Match* match, Sequence&& source) {
    if constexpr (std::same_as<std::remove_cvref_t<Sequence>, CodeBuilder>) {
      return apply_sequence(at, placement, match, {}, &source);
    } else if constexpr (std::convertible_to<Sequence&, std::span<const MethodInstruction>>) {
      return apply_sequence(at, placement, match, source, nullptr);
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
      return apply_sequence(at, placement, match, {}, &builder);
    }
  }
  /// @param match Saved instruction identities and source revision.
  /// @return Validated current start ordinal or a stale match error.
  [[nodiscard]] std::expected<std::size_t, BlockEditError> match_position(const pattern::Match& match) const;
  /// Resolve operands and publish one sequence under a single class and method transaction.
  /// @param anchor Insertion anchor when inserting by handle.
  /// @param placement Edit position relative to the supplied handle.
  /// @param match Saved range to replace, or null for insertion.
  /// @param instructions Instructions supplied directly by the caller.
  /// @param builder Optional owner supplying instructions and label fixups.
  /// @return Handles produced by the edit or an indexed failure.
  SequenceResult apply_sequence(CodeNode anchor, Placement placement, const pattern::Match* match,
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
