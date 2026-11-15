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

struct BlockEditError {
  CodeEditError cause;
  // Zero-based position in the patch, when the failure belongs to an instruction.
  std::optional<std::size_t> instruction_index;
};

struct InstructionEntry {
  InstructionId id;
  Instruction instruction;
};

class MethodCode {
public:
  class InstructionIterator {
  public:
    using iterator_concept = std::forward_iterator_tag;
    using value_type = InstructionEntry;
    using difference_type = std::ptrdiff_t;

    InstructionIterator() noexcept : InstructionIterator(nullptr, 0) {
    }
    [[nodiscard]] InstructionEntry operator*() const noexcept;
    InstructionIterator& operator++() noexcept;
    InstructionIterator operator++(int) noexcept;
    friend bool operator==(const InstructionIterator&, const InstructionIterator&) = default;

  private:
    friend class MethodCode;
    InstructionIterator(const MethodCode* code, std::size_t position) noexcept :
        code(code), position(position) {
    }

    const MethodCode* code;
    std::size_t position;
  };
  using InstructionRange = std::ranges::subrange<InstructionIterator, InstructionIterator, std::ranges::subrange_kind::sized>;

  // The resource must outlive the method.
  explicit MethodCode(std::pmr::memory_resource* resource = std::pmr::get_default_resource());
  ~MethodCode();
  MethodCode(const MethodCode&) = delete;
  MethodCode& operator=(const MethodCode&) = delete;
  MethodCode(MethodCode&&) = delete;
  MethodCode& operator=(MethodCode&&) = delete;

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] bool edited() const {
    return revision() != 0;
  }

  // Traversal follows method order. An out-of-range position returns an
  // invalid handle, which get() and all edit operations reject.
  [[nodiscard]] InstructionId at(std::size_t position) const;
  // Return by value so the result survives slot vector growth. Branch and
  // switch operands are inspected through the method-scoped accessors below.
  [[nodiscard]] std::expected<Instruction, CodeEditError> get(InstructionId id) const;
  // Entries are values. Keep the method alive and acquire a fresh range after editing.
  [[nodiscard]] InstructionRange instructions() const noexcept;
  void reserve(std::size_t count);

  [[nodiscard]] std::expected<InstructionId, CodeEditError> append(const InstructionBuilder& value);
  // Block overloads return handles in input order and commit as one edit.
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> append(Block&& block) {
    return insert_block(size(), std::forward<Block>(block));
  }
  // Existing labels stay on the old instruction.
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before(InstructionId at, const InstructionBuilder& value);
  // Insert a block in input order or leave the method as it was. The returned
  // handles follow input order. An empty block at a valid anchor is a no-op.
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_before(InstructionId at, Block&& block) {
    return insert_block_before(at, std::forward<Block>(block));
  }
  // Rebinds the selected label to the inserted instruction. Other labels at
  // the same boundary keep their former bindings.
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_at(LabelId at, const InstructionBuilder& value);
  // A nonempty block rebinds the selected label to its first instruction.
  template<typename Block>
    requires std::convertible_to<Block&&, std::span<const InstructionBuilder>>
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_at(LabelId at, Block&& block) {
    return insert_block_at(at, std::forward<Block>(block));
  }
  [[nodiscard]] std::expected<void, CodeEditError> replace(InstructionId id, const InstructionBuilder& value);
  // Labeled instructions must be explicitly retargeted before deletion.
  [[nodiscard]] std::expected<void, CodeEditError> erase(InstructionId id);
  [[nodiscard]] std::expected<void, CodeEditError> move_before(InstructionId id, CodePosition position);

  // New labels start unbound, so builders can refer to forward destinations.
  [[nodiscard]] std::expected<LabelId, CodeEditError> make_label();
  [[nodiscard]] std::expected<void, CodeEditError> bind_label(LabelId id, CodePosition position);
  [[nodiscard]] std::expected<CodePosition, CodeEditError> label_position(LabelId id) const;
  [[nodiscard]] std::expected<LabelId, CodeEditError> branch_target(InstructionId id) const;
  [[nodiscard]] std::expected<SwitchId, CodeEditError> switch_id(InstructionId id) const;
  // Views into owned operand storage are valid until code mutation.
  [[nodiscard]] std::expected<TableSwitch, CodeEditError> table_switch(SwitchId id) const;
  [[nodiscard]] std::expected<LookupSwitch, CodeEditError> lookup_switch(SwitchId id) const;

private:
  struct Storage;
  struct StorageDeleter {
    void operator()(Storage* value) const;
  };
  friend struct detail::BytecodeAccess;

  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert(std::size_t position, const InstructionBuilder& value);
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block_before(
      InstructionId at, std::span<const InstructionBuilder> block);
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block_at(
      LabelId at, std::span<const InstructionBuilder> block);
  [[nodiscard]] std::expected<std::vector<InstructionId>, BlockEditError> insert_block(
      std::size_t position, std::span<const InstructionBuilder> block);
  [[nodiscard]] std::expected<Instruction, CodeEditError> build(const InstructionBuilder& value);
  template<typename AddSwitch>
  [[nodiscard]] std::expected<Instruction, CodeEditError> build(const InstructionBuilder& value, AddSwitch&& add_switch);
  [[nodiscard]] std::expected<LabelId, CodeEditError> add_label(std::optional<CodePosition> position);
  void release_operand(Instruction value);

  std::unique_ptr<Storage, StorageDeleter> storage;
};

} // namespace cafe
