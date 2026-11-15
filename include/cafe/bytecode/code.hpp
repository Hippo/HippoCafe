/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "cafe/bytecode/builder.hpp"
#include "cafe/bytecode/label.hpp"

namespace cafe {

/**
 * @brief Failure of an atomic instruction-block edit, optionally identifying the offending patch
 * element.
 * @ingroup bytecode
 */
struct BlockEditError {
  CodeEditError cause; ///< Underlying instruction edit failure.
  /**
   * @brief Zero-based failing position in a submitted patch, when known.
   *
   * Zero-based position in the patch, when the failure belongs to an instruction.
   */
  std::optional<std::size_t> instruction_index;
};

/**
 * @brief An instruction value paired with its stable handle for subsequent edits.
 * @ingroup bytecode
 */
struct InstructionEntry {
  InstructionId id; ///< Stable handle of the associated instruction.
  Instruction instruction; ///< Copied instruction value; branch and switch operands remain method-scoped.
};

/**
 * @brief Owns ordered instructions, scoped labels, and switch operands.
 *
 * Instruction values are copies; ranges and operand views must be reacquired after mutation.
 * Handles survive unrelated edits but not erasure or owner destruction. The resource must outlive
 * the method; concurrent mutation requires external synchronization. Allocating edits may throw
 * std::bad_alloc.
 * @ingroup bytecode
 */
class MethodCode {
public:
  /**
   * @brief Forward iterator over instruction values in method order.
   *
   * Dereference requires a live method, a non-end position, and no intervening mutation.
   * @ingroup bytecode
   */
  class InstructionIterator {
  public:
    /**
     * @brief Forward-iterator category for instruction traversal.
     */
    using iterator_concept = std::forward_iterator_tag;
    /**
     * @brief Value returned when dereferencing an instruction iterator.
     */
    using value_type = InstructionEntry;
    /**
     * @brief Signed distance type used by standard iterator machinery.
     */
    using difference_type = std::ptrdiff_t;

    /**
     * @brief Create a default iterator or an iterator at a specified method-order position.
     */
    InstructionIterator() noexcept : InstructionIterator(nullptr, 0) {
    }
    /**
     * @brief Read the current instruction and handle by value.
     * @return An independent entry; dereferencing end or an invalidated iterator is not permitted.
     */
    [[nodiscard]] InstructionEntry operator*() const noexcept;
    /**
     * @brief Advance by one method-order position.
     * @return The advanced iterator for prefix increment, or its previous value for postfix increment.
     */
    InstructionIterator& operator++() noexcept;
    /**
     * @brief Advance by one method-order position.
     * @return The advanced iterator for prefix increment, or its previous value for postfix increment.
     */
    InstructionIterator operator++(int) noexcept;
    friend bool operator==(const InstructionIterator&, const InstructionIterator&) = default;

  private:
    friend class MethodCode;
    /**
     * @brief Create a default iterator or an iterator at a specified method-order position.
     * @param code Decoded method instruction sequence to inspect.
     * @param position Instruction-order position or explicit code boundary.
     */
    InstructionIterator(const MethodCode* code, std::size_t position) noexcept :
        code(code), position(position) {
    }

    const MethodCode* code; ///< Borrowed owning method; null for a default-constructed iterator.
    std::size_t position; ///< Current index in method instruction order.
  };
  /**
   * @brief Sized forward range of instruction values in method order.
   */
  using InstructionRange = std::ranges::subrange<InstructionIterator, InstructionIterator, std::ranges::subrange_kind::sized>;

  /**
   * @brief Create an empty instruction owner with a fresh handle scope.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @pre resource is non-null.
   */
  explicit MethodCode(std::pmr::memory_resource* resource = std::pmr::get_default_resource());
  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~MethodCode();
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  MethodCode(const MethodCode&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  MethodCode& operator=(const MethodCode&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  MethodCode(MethodCode&&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  MethodCode& operator=(MethodCode&&) = delete;

  /**
   * @brief Count live instructions in method order.
   * @return The number of live instructions, excluding erased slots.
   */
  [[nodiscard]] std::size_t size() const;
  /**
   * @brief Read the mutation revision.
   * @return Zero for pristine decoded code; successful edits advance the revision.
   */
  [[nodiscard]] std::uint64_t revision() const;
  /**
   * @brief Test whether instructions or labels have been edited.
   * @return True when the revision is nonzero.
   */
  [[nodiscard]] bool edited() const {
    return revision() != 0;
  }

  /**
   * @brief Find the instruction handle at a method-order position.
   * @param position Instruction-order position or explicit code boundary.
   * @return A live handle, or an invalid zero-generation handle when out of range.
   *
   * Traversal follows method order. An out-of-range position returns an
   * invalid handle, which get() and all edit operations reject.
   */
  [[nodiscard]] InstructionId at(std::size_t position) const;
  /**
   * @brief Copy the instruction identified by a stable handle.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return An independent value, or InvalidInstruction for stale or foreign handles.
   *
   * Return by value so the result survives slot vector growth. Branch and
   * switch operands are inspected through the method-scoped accessors below.
   */
  [[nodiscard]] std::expected<Instruction, CodeEditError> get(InstructionId id) const;
  /**
   * @brief Traverse instruction values in method order.
   * @return A sized borrowed range; acquire a new range after any mutation.
   */
  [[nodiscard]] InstructionRange instructions() const noexcept;
  /**
   * @brief Reserve instruction-order and slot capacity without changing logical contents.
   * @param count Desired total instruction capacity; this is not an additional count.
   */
  void reserve(std::size_t count);

  /**
   * @brief Append one instruction at the method end.
   *
   * Pass an Instruction or another InstructionBuilder alternative directly. Success advances the
   * revision once. Errors and allocation failure leave logical contents unchanged; allocation
   * exceptions may propagate. Existing instruction handles remain valid.
   * @param value Instruction descriptor to copy into this method; switch operand views are copied.
   * @return The new instruction handle, or a CodeEditError describing an invalid anchor, operand,
   *   label ownership, or exhausted handle/revision space.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> append(const InstructionBuilder& value);
  /**
   * @brief Append an instruction block atomically at the method end.
   *
   * Instructions keep input order. An empty block is a no-op.
   * Success advances the revision once. Errors and allocation failure leave logical contents
   * unchanged; allocation exceptions may propagate. Existing instruction handles remain valid.
   * @param block Contiguous range of InstructionBuilder values, such as std::array or std::span.
   * @return New handles in input order, or a BlockEditError with the failing patch index when
   *   available; anchor errors have no patch index.
   */
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> append(Block&& block) {
    return insert_block(size(), std::forward<Block>(block));
  }
  /**
   * @brief Insert one instruction before an existing instruction, leaving its labels attached.
   *
   * Pass an Instruction or another InstructionBuilder alternative directly. Success advances the
   * revision once. Errors and allocation failure leave logical contents unchanged; allocation
   * exceptions may propagate. Existing instruction handles remain valid.
   * @param at Live instruction handle in this method.
   * @param value Instruction descriptor to copy into this method; switch operand views are copied.
   * @return The new instruction handle, or a CodeEditError describing an invalid anchor, operand,
   *   label ownership, or exhausted handle/revision space.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before(InstructionId at, const InstructionBuilder& value);
  /**
   * @brief Insert an instruction block atomically before an existing instruction.
   *
   * Instructions keep input order; labels remain on the original anchor. An empty block
   * requires a valid anchor and is otherwise a no-op.
   * Success advances the revision once. Errors and allocation failure leave logical contents
   * unchanged; allocation exceptions may propagate. Existing instruction handles remain valid.
   * @param block Contiguous range of InstructionBuilder values, such as std::array or std::span.
   * @param at Live instruction handle in this method.
   * @return New handles in input order, or a BlockEditError with the failing patch index when
   *   available; anchor errors have no patch index.
   */
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_before(InstructionId at, Block&& block) {
    return insert_block_before(at, std::forward<Block>(block));
  }
  /**
   * @brief Insert one instruction at a bound label and rebind only that label to it.
   *
   * Pass an Instruction or another InstructionBuilder alternative directly. Success advances the
   * revision once. Errors and allocation failure leave logical contents unchanged; allocation
   * exceptions may propagate. Existing instruction handles remain valid.
   * @param at Bound label in this method; the exclusive end is permitted.
   * @param value Instruction descriptor to copy into this method; switch operand views are copied.
   * @return The new instruction handle, or a CodeEditError describing an invalid anchor, operand,
   *   label ownership, or exhausted handle/revision space.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_at(LabelId at, const InstructionBuilder& value);
  /**
   * @brief Insert an instruction block atomically at a bound label.
   *
   * Instructions keep input order. A nonempty block rebinds only the selected label to its
   * first instruction. An empty block requires a bound label and leaves its binding unchanged.
   * Success advances the revision once. Errors and allocation failure leave logical contents
   * unchanged; allocation exceptions may propagate. Existing instruction handles remain valid.
   * @param block Contiguous range of InstructionBuilder values, such as std::array or std::span.
   * @param at Bound label in this method; the exclusive end is permitted.
   * @return New handles in input order, or a BlockEditError with the failing patch index when
   *   available; anchor errors have no patch index.
   */
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_at(LabelId at, Block&& block) {
    return insert_block_at(at, std::forward<Block>(block));
  }
  /**
   * @brief Replace an instruction's value while retaining its identity.
   * @param id Stable instruction handle belonging to the supplied method.
   * @param value Instruction descriptor to validate and copy into this method.
   * @return Success or an edit error; existing labels retain their bindings.
   */
  [[nodiscard]] std::expected<void, CodeEditError> replace(InstructionId id, const InstructionBuilder& value);
  /**
   * @brief Erase an instruction that is not the destination of a bound label.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return Success or InvalidInstruction/LabeledInstruction; the erased handle becomes stale.
   *
   * Labeled instructions must be explicitly retargeted before deletion.
   */
  [[nodiscard]] std::expected<void, CodeEditError> erase(InstructionId id);
  /**
   * @brief Move an instruction to a different boundary in method order.
   * @param id Stable instruction handle belonging to the supplied method.
   * @param position Instruction-order position or explicit code boundary.
   * @return Success or an invalid-instruction/position error; identity and attached labels are
   *   preserved.
   */
  [[nodiscard]] std::expected<void, CodeEditError> move_before(InstructionId id, CodePosition position);

  /**
   * @brief Allocate a new unbound method-local label.
   * @return A label usable by forward branch builders, or HandleLimit.
   *
   * New labels start unbound, so builders can refer to forward destinations.
   */
  [[nodiscard]] std::expected<LabelId, CodeEditError> make_label();
  /**
   * @brief Bind a label to an instruction boundary or the exclusive end.
   * @param id Label handle scoped to the supplied method.
   * @param position Instruction-order position or explicit code boundary.
   * @return Success or an ownership/handle-limit error.
   */
  [[nodiscard]] std::expected<void, CodeEditError> bind_label(LabelId id, CodePosition position);
  /**
   * @brief Resolve a bound label to a code boundary.
   * @param id Label handle scoped to the supplied method.
   * @return Its position, or InvalidLabel/UnboundLabel.
   */
  [[nodiscard]] std::expected<CodePosition, CodeEditError> label_position(LabelId id) const;
  /**
   * @brief Inspect a branch operand through its owner.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return The scoped destination label, or a handle/operand-form error.
   */
  [[nodiscard]] std::expected<LabelId, CodeEditError> branch_target(InstructionId id) const;
  /**
   * @brief Inspect the scoped operand handle of a switch instruction.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return Its SwitchId, or a handle/operand-form error.
   */
  [[nodiscard]] std::expected<SwitchId, CodeEditError> switch_id(InstructionId id) const;
  /**
   * @brief Borrow dense switch operands.
   * @param id Switch operand handle scoped to the supplied method.
   * @return A view valid until code mutation, or InvalidSwitch/WrongOperandForm.
   *
   * Views into owned operand storage are valid until code mutation.
   */
  [[nodiscard]] std::expected<TableSwitch, CodeEditError> table_switch(SwitchId id) const;
  /**
   * @brief Borrow sparse switch operands.
   * @param id Switch operand handle scoped to the supplied method.
   * @return A view valid until code mutation, or InvalidSwitch/WrongOperandForm.
   */
  [[nodiscard]] std::expected<LookupSwitch, CodeEditError> lookup_switch(SwitchId id) const;

private:
  struct Storage;
  /**
   * @brief Releases method storage through its original PMR resource.
   * @ingroup bytecode
   */
  struct StorageDeleter {
    /**
     * @brief Destroy and deallocate the payload through its original PMR resource.
     * @param value Storage to destroy through its original allocation resource.
     */
    void operator()(Storage* value) const;
  };
  friend struct detail::BytecodeAccess;

  /**
   * @brief Insert one validated instruction at an order-vector position.
   * @param position Instruction-order position or explicit code boundary.
   * @param value Instruction descriptor to validate and copy into this method.
   * @return Its new handle or an edit error; allocation is staged before publication.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert(std::size_t position, const InstructionBuilder& value);
  /**
   * @brief Resolve an instruction anchor for atomic block insertion.
   * @param at Stable instruction handle belonging to the supplied method.
   * @param block Instruction descriptors inserted atomically in input order.
   * @return New handles in patch order, or a BlockEditError.
   */
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block_before(
      InstructionId at, std::span<const InstructionBuilder> block);
  /**
   * @brief Resolve a label anchor for atomic block insertion.
   * @param at Label handle scoped to the supplied method.
   * @param block Instruction descriptors inserted atomically in input order.
   * @return New handles in patch order, or a BlockEditError.
   */
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block_at(
      LabelId at, std::span<const InstructionBuilder> block);
  /**
   * @brief Stage handles and operand copies, then commit a block with one order-vector shift.
   * @param position Instruction-order position or explicit code boundary.
   * @param block Instruction descriptors inserted atomically in input order.
   * @return New handles in patch order, or an error leaving the original method unchanged.
   */
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block(
      std::size_t position, std::span<const InstructionBuilder> block);
  /**
   * @brief Validate a builder and install any owned switch operand.
   * @param value Instruction descriptor to validate and copy into this method.
   * @return A compact instruction, or a form/ownership/handle-limit error.
   */
  [[nodiscard]] std::expected<Instruction, CodeEditError> build(const InstructionBuilder& value);
  /**
   * @brief Validate a builder and install any owned switch operand.
   * @param value Instruction descriptor to validate and copy into this method.
   * @param add_switch Callback that stages owned switch operands and returns a compact operand
   *   index.
   * @return A compact instruction, or a form/ownership/handle-limit error.
   */
  template<typename AddSwitch>
  [[nodiscard]] std::expected<Instruction, CodeEditError> build(const InstructionBuilder& value, AddSwitch&& add_switch);
  /**
   * @brief Allocate a scoped label with an optional binding.
   * @param position Instruction-order position or explicit code boundary.
   * @return The new label, or HandleLimit.
   */
  [[nodiscard]] std::expected<LabelId, CodeEditError> add_label(std::optional<CodePosition> position);
  /**
   * @brief Release an instruction's out-of-line switch operand, if any.
   * @param value Instruction whose switch operand is no longer in use.
   */
  void release_operand(Instruction value);

  std::unique_ptr<Storage, StorageDeleter> storage; ///< Owned backing storage for this object and its borrowed views.
};

} // namespace cafe
