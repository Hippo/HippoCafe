/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup implementation
 */

#include "cafe/bytecode/code.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "code_access.hpp"

namespace cafe {
namespace {

constexpr std::uint32_t NO_SLOT = std::numeric_limits<std::uint32_t>::max(); ///< Free-list sentinel indicating that no reusable slot exists.
constexpr std::uint32_t SWITCH_SLOT_COUNT = 1u << 16; ///< Number of operand slots representable by the packed switch handle.
constexpr InstructionId UNBOUND{}; ///< Label-table sentinel for a label with no boundary binding.
constexpr InstructionId EXCLUSIVE_END{NO_SLOT, 0}; ///< Label-table sentinel identifying the boundary after the last instruction.

/// Instruction slots and label indices use disjoint key ranges in the same sparse edit history.
using EditStamps = std::pmr::map<std::uint64_t, std::uint64_t>;
constexpr std::uint64_t LABEL_KEY_BASE = std::uint64_t{1} << 32; ///< First label key, beyond every instruction slot.

/// @param label Method-scoped label whose binding may change.
/// @return Sparse stamp key for that binding.
constexpr std::uint64_t binding_key(LabelId label) {
  return LABEL_KEY_BASE + label.value;
}

/**
 * @brief Allocate a process-wide nonwrapping handle generation.
 * @return A fresh nonzero generation, or zero when exhausted.
 * @ingroup implementation
 */
std::uint32_t acquire_generation() {
  // A method's fresh slots share its initial generation. Each reused slot
  // gets a fresh generation. Stop issuing handles if the serial space runs out.
  static std::atomic<std::uint32_t> next{1};
  auto generation = next.load(std::memory_order_relaxed);
  while (generation != 0) {
    auto successor = generation == NO_SLOT ? 0 : generation + 1;
    if (next.compare_exchange_weak(generation, successor, std::memory_order_relaxed))
      return generation;
  }
  return 0;
}

/**
 * @brief Grow a vector geometrically before a transactional edit without exceeding max_size().
 * @param values Sequence of values to copy or emit.
 * @param count Number of additional elements to reserve beyond the current size.
 * @ingroup implementation
 */
template<typename Value>
void reserve_additional(std::pmr::vector<Value>& values, std::size_t count) {
  if (count <= values.capacity() - values.size())
    return;
  const auto available = values.max_size() - values.size();
  if (count > available)
    throw std::length_error("instruction storage capacity exhausted");
  values.reserve(values.size() + std::max(count, std::min(values.size() + 1, available)));
}

/**
 * @brief Compact instruction, generation, and free-list link independent of method traversal
 * order.
 * @ingroup implementation
 */
struct InstructionSlot {
  Instruction instruction; ///< Copied compact instruction value stored in this live slot.
  std::uint32_t generation; ///< Nonzero handle generation; changes when a slot is reused.
  std::uint32_t next_free = NO_SLOT; ///< Next reusable slot index, or NO_SLOT at the end of the free list.
};

static_assert(sizeof(InstructionId) == 8);
static_assert(sizeof(InstructionSlot) == 16);

/**
 * @brief Allocator-owned consecutive switch targets and original padding bytes.
 * @ingroup implementation
 */
struct OwnedTableSwitch {
  /**
   * @brief Initialize allocator-owned consecutive switch targets and original padding bytes.
   * @param view Borrowed switch descriptor copied into owned operand storage.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @ingroup implementation
   */
  OwnedTableSwitch(TableSwitch view, std::pmr::memory_resource* resource) :
      low(view.low), default_target(view.default_target), targets(resource) {
    if (!view.targets.empty())
      targets.assign(view.targets.begin(), view.targets.end());
  }

  std::int32_t low; ///< First signed dense-switch key; targets correspond to consecutive keys.
  LabelId default_target; ///< Destination used when no case key matches.
  std::pmr::vector<LabelId> targets; ///< Owned destination labels for consecutive keys beginning at low.
};

/**
 * @brief Allocator-owned sorted switch pairs and original padding bytes.
 * @ingroup implementation
 */
struct OwnedLookupSwitch {
  /**
   * @brief Initialize allocator-owned sorted switch pairs and original padding bytes.
   * @param view Borrowed switch descriptor copied into owned operand storage.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @ingroup implementation
   */
  OwnedLookupSwitch(LookupSwitch view, std::pmr::memory_resource* resource) :
      default_target(view.default_target), pairs(resource) {
    if (!view.pairs.empty())
      pairs.assign(view.pairs.begin(), view.pairs.end());
  }

  LabelId default_target; ///< Destination used when no case key matches.
  std::pmr::vector<LookupPair> pairs; ///< Owned lookup entries in strictly increasing signed-key order.
};

/**
 * @brief Empty/free slot or an owned dense/sparse switch operand.
 */
using SwitchPayload = std::variant<std::monostate, OwnedTableSwitch, OwnedLookupSwitch>;
static_assert(std::is_nothrow_move_constructible_v<SwitchPayload>);

/**
 * @brief Copy a borrowed switch descriptor into allocator-owned operand storage.
 * @param view Borrowed switch descriptor copied into owned operand storage.
 * @param resource PMR resource that must outlive the allocated object or returned storage.
 * @return An owned dense or sparse switch alternative.
 * @ingroup implementation
 */
template<typename View>
SwitchPayload own_switch(View view, std::pmr::memory_resource* resource) {
  using Payload = std::conditional_t<std::is_same_v<View, TableSwitch>, OwnedTableSwitch, OwnedLookupSwitch>;
  return SwitchPayload(std::in_place_type<Payload>, view, resource);
}

/// Materialize symbolic destinations while preserving switch key order.
/// @param value Branch or switch with pending target operands.
/// @param fixups Nonempty list of (instruction index, label ordinal) pairs, switch default first.
/// @param resolve Callback resolving a local label ordinal in the pending patch.
/// @param build Callback staging a concrete branch or switch in method storage.
/// @return Resolved instruction or an invalid target or staging error.
template<typename Resolve, typename Build>
std::expected<Instruction, CodeEditError> build_local_targets(
    const InstructionBuilder& value, std::span<const std::pair<std::size_t, std::size_t>> fixups,
    Resolve&& resolve, Build&& build) {
  const auto* branch = std::get_if<Branch>(&value);
  const auto* table = std::get_if<TableSwitch>(&value);
  const auto* lookup = std::get_if<LookupSwitch>(&value);
  if (!branch && !table && !lookup)
    return std::unexpected(CodeEditError::WrongOperandForm);
  const auto case_count = table ? table->targets.size() : lookup ? lookup->pairs.size()
                                                                 : 0;
  if (fixups.size() - 1 != case_count)
    return std::unexpected(CodeEditError::InvalidLabel);
  const auto target = resolve(fixups.front().second);
  if (!target)
    return std::unexpected(target.error());
  if (branch)
    return build(Branch{branch->opcode, *target, branch->encoding_hint});
  if (table) {
    std::vector<LabelId> cases;
    cases.reserve(case_count);
    for (const auto& fixup : fixups.subspan(1)) {
      const auto label = resolve(fixup.second);
      if (!label)
        return std::unexpected(label.error());
      cases.push_back(*label);
    }
    return build(TableSwitch{table->low, *target, cases});
  }
  std::vector<LookupPair> cases;
  cases.reserve(case_count);
  for (std::size_t i = 0; i < case_count; ++i) {
    const auto label = resolve(fixups[i + 1].second);
    if (!label)
      return std::unexpected(label.error());
    cases.push_back({lookup->pairs[i].key, *label});
  }
  return build(LookupSwitch{*target, cases});
}

/**
 * @brief Reusable out-of-line switch operand slot with generation and free-list linkage.
 * @ingroup implementation
 */
struct SwitchSlot {
  SwitchPayload payload; ///< Owned dense or sparse switch operands; monostate marks an unused slot.
  std::uint32_t next_free = NO_SLOT; ///< Next reusable slot index, or NO_SLOT at the end of the free list.
  std::uint16_t generation = 1; ///< Nonzero handle generation; changes when a slot is reused.
  /**
   * @brief Retained switch-alignment bytes from the original code.
   *
   * Since version 51, JVMs ignore the literal padding octets.
   */
  std::array<std::byte, 3> original_padding{};
  std::uint8_t padding_length = 0; ///< Number of meaningful retained switch padding bytes.
};

/**
 * @brief Pack a switch slot index and generation into its compact operand.
 * @param index Zero-based switch operand storage slot.
 * @param generation Nonzero slot generation used to reject stale handles.
 * @return The encoded switch handle value.
 * @ingroup implementation
 */
std::uint32_t switch_value(std::uint32_t index, std::uint16_t generation) {
  return (static_cast<std::uint32_t>(generation) << 16) | index;
}

/**
 * @brief Extract the slot portion of a packed switch operand.
 * @param value Packed switch operand containing its index and lookup/table discriminator.
 * @return A zero-based switch storage index.
 * @ingroup implementation
 */
std::uint32_t switch_index(std::uint32_t value) {
  return value & (SWITCH_SLOT_COUNT - 1);
}

} // namespace

/**
 * @brief PMR-backed instruction slots, order, labels, and switch operands. Capacity is staged
 * before edits publish new live slots; scope and revisions never wrap into reusable identities.
 * @ingroup implementation
 */
struct MethodCode::Storage {
  /**
   * @brief Initialize PMR-backed instruction slots, order, labels, and switch operands
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  explicit Storage(std::pmr::memory_resource* resource) :
      resource(resource), slots(resource), order(resource), labels(resource), label_order(resource), switches(resource), changes(resource) {
  }

  /**
   * @brief Validate a scoped instruction or label handle against current storage.
   * @param id Instruction handle checked for scope, slot, generation, and liveness.
   * @return True only for a live handle belonging to this owner.
   */
  [[nodiscard]] bool contains(InstructionId id) const {
    return id.generation != 0 && id.index < slots.size() && slots[id.index].generation == id.generation;
  }

  /**
   * @brief Validate a scoped instruction or label handle against current storage.
   * @param id Label handle scoped to the supplied method.
   * @return True only for a live handle belonging to this owner.
   */
  [[nodiscard]] bool contains(LabelId id) const {
    return scope != 0 && id.scope == scope && id.value < labels.size();
  }

  /**
   * @brief Validate a scoped switch handle and locate its operand slot.
   * @param id Switch operand handle scoped to the supplied method.
   * @return A borrowed slot pointer, or nullptr for a stale/foreign handle.
   */
  [[nodiscard]] const SwitchSlot* find_switch(SwitchId id) const {
    auto index = switch_index(id.value);
    if (scope == 0 || id.scope != scope || index >= switches.size())
      return nullptr;
    const auto& slot = switches[index];
    if (slot.generation != (id.value >> 16) || std::holds_alternative<std::monostate>(slot.payload))
      return nullptr;
    return &slot;
  }

  /**
   * @brief Check whether scope and revision space still permit mutation.
   * @return True unless handle generation or revision space is exhausted.
   */
  [[nodiscard]] bool can_edit() const {
    return scope != 0 && revision != std::numeric_limits<std::uint64_t>::max();
  }

  /// Reserve a new stamp node outside live state before an edit with further failure points.
  /// @param staged Nodes to merge on successful publication.
  /// @param key Instruction slot or tagged label binding.
  void stage_stamp(EditStamps& staged, std::uint64_t key) const {
    if (!changes.contains(key))
      staged.try_emplace(key, revision + 1);
  }

  /// Publish a previously staged binding update alongside an instruction insertion.
  /// @param label Label whose move was staged.
  /// @param target New entry instruction.
  /// @param stamp Revision of the insertion.
  void publish_binding(LabelId label, InstructionId target, std::uint64_t stamp) {
    changes.at(binding_key(label)) = stamp;
    labels[label.value] = target;
  }

  /**
   * @brief Copy and install a switch descriptor using available operand storage.
   * @param view Borrowed switch descriptor copied into owned operand storage.
   * @return Its packed operand value, or HandleLimit.
   */
  template<typename View>
  [[nodiscard]] std::expected<std::uint32_t, CodeEditError> add_switch(View view) {
    if (free_switch == NO_SLOT && switches.size() >= SWITCH_SLOT_COUNT)
      return std::unexpected(CodeEditError::HandleLimit);
    return install_switch(own_switch(view, resource));
  }

  /**
   * @brief Publish an already owned switch payload into a new or reusable slot.
   * @param payload Owned dense or sparse switch operands to store in a method slot.
   * @return The packed slot/generation operand value.
   */
  std::uint32_t install_switch(SwitchPayload payload) {
    if (free_switch == NO_SLOT) {
      auto index = static_cast<std::uint32_t>(switches.size());
      switches.push_back(SwitchSlot{std::move(payload)});
      return switch_value(index, switches.back().generation);
    }

    auto index = free_switch;
    auto& slot = switches[index];
    free_switch = slot.next_free;
    slot.next_free = NO_SLOT;
    // The free slot holds monostate, so assignment move-constructs the payload.
    slot.payload = std::move(payload);
    return switch_value(index, slot.generation);
  }

  /**
   * @brief Publish a prepared instruction into a new or reusable slot.
   * @param id Stable instruction handle belonging to the supplied method.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   */
  void install_instruction(InstructionId id, Instruction instruction) {
    if (id.index == slots.size()) {
      slots.push_back({instruction, id.generation});
    } else {
      auto& slot = slots[id.index];
      free_instruction = slot.next_free;
      slot = InstructionSlot{instruction, id.generation};
    }
  }

  std::pmr::memory_resource* resource; ///< Borrowed PMR resource that must outlive dependent storage.
  std::pmr::vector<InstructionSlot> slots; ///< Allocated instruction slots, including reusable erased slots.
  std::pmr::vector<std::uint32_t> order; ///< Live slot indices in method traversal order.
  std::pmr::vector<InstructionId> labels; ///< Method-scoped label bindings indexed by label value.
  std::pmr::vector<std::uint32_t> label_order; ///< Label values in relative order within each boundary.
  std::pmr::vector<SwitchSlot> switches; ///< Out-of-line switch operand slots with generation/free-list metadata.
  EditStamps changes; ///< Last edit to each replaced instruction or rebound label, stored sparsely.
  std::uint32_t free_instruction = NO_SLOT; ///< Head of the reusable instruction-slot free list.
  std::uint32_t free_switch = NO_SLOT; ///< Head of the reusable switch-slot free list.
  std::uint32_t scope = acquire_generation(); ///< Method identity used to reject foreign labels or switches.
  std::uint64_t revision = 0; ///< Monotonic code revision advanced once per successful edit.
};

MethodCode::MethodCode(std::pmr::memory_resource* resource) :
    storage(std::pmr::polymorphic_allocator<Storage>(resource).new_object<Storage>(resource)) {
}

MethodCode::~MethodCode() = default;

void MethodCode::StorageDeleter::operator()(Storage* value) const {
  std::pmr::polymorphic_allocator<Storage>(value->resource).delete_object(value);
}

std::size_t MethodCode::size() const {
  return storage->order.size();
}

std::uint64_t MethodCode::revision() const {
  return storage->revision;
}

InstructionId MethodCode::at(std::size_t position) const {
  if (position >= size())
    return {};
  auto index = storage->order[position];
  return {index, storage->slots[index].generation};
}

std::expected<Instruction, CodeEditError> MethodCode::get(InstructionId id) const {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidInstruction);
  return storage->slots[id.index].instruction;
}

InstructionEntry MethodCode::InstructionIterator::operator*() const noexcept {
  const auto index = code->storage->order[position];
  const auto& slot = code->storage->slots[index];
  return {{index, slot.generation}, slot.instruction};
}

MethodCode::InstructionIterator& MethodCode::InstructionIterator::operator++() noexcept {
  ++position;
  return *this;
}

MethodCode::InstructionIterator MethodCode::InstructionIterator::operator++(int) noexcept {
  auto previous = *this;
  ++*this;
  return previous;
}

MethodCode::InstructionRange MethodCode::instructions() const noexcept {
  return {InstructionIterator(this, 0), InstructionIterator(this, size()), size()};
}

std::vector<CodeNode> MethodCode::nodes() const {
  std::vector<CodeNode> nodes;
  nodes.reserve(size() + storage->labels.size());
  if (storage->labels.empty()) {
    for (const auto entry : instructions())
      nodes.emplace_back(entry.id);
    return nodes;
  }
  // Group labels by their following instruction and preserve the order of aliases at each boundary.
  std::vector<std::uint32_t> first(storage->slots.size() + 1, NO_SLOT);
  std::vector<std::uint32_t> next(storage->labels.size(), NO_SLOT);
  const auto end = storage->slots.size();
  for (const auto label : storage->label_order | std::views::reverse) {
    const auto target = storage->labels[label];
    if (target != UNBOUND) {
      auto& head = first[target == EXCLUSIVE_END ? end : target.index];
      next[label] = std::exchange(head, label);
    }
  }
  const auto append_labels = [&](std::size_t boundary) {
    for (auto label = first[boundary]; label != NO_SLOT; label = next[label])
      nodes.emplace_back(LabelId{label, storage->scope});
  };
  for (const auto entry : instructions()) {
    append_labels(entry.id.index);
    nodes.emplace_back(entry.id);
  }
  append_labels(end);
  return nodes;
}

void MethodCode::reserve(std::size_t count) {
  storage->order.reserve(count);
  storage->slots.reserve(count);
}

std::expected<InstructionId, CodeEditError> MethodCode::append(const InstructionBuilder& value) {
  return insert(size(), value);
}

std::expected<InstructionId, CodeEditError> MethodCode::insert_before(InstructionId id, const InstructionBuilder& value) {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidInstruction);
  auto at = std::ranges::find(storage->order, id.index);
  return insert(static_cast<std::size_t>(at - storage->order.begin()), value);
}

std::expected<InstructionId, CodeEditError> MethodCode::insert_at(LabelId id, const InstructionBuilder& value) {
  auto position = label_position(id);
  if (!position)
    return std::unexpected(position.error());
  EditStamps staged(storage->resource);
  storage->stage_stamp(staged, binding_key(id));
  auto inserted = position->before ? insert_before(*position->before, value) : append(value);
  if (inserted) {
    storage->changes.merge(staged);
    storage->publish_binding(id, *inserted, storage->revision);
  }
  return inserted;
}

std::expected<InstructionId, CodeEditError> MethodCode::insert(std::size_t position, const InstructionBuilder& value) {
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);
  auto index = storage->free_instruction;
  bool reuse = index != NO_SLOT;
  if (!reuse && storage->slots.size() >= NO_SLOT)
    return std::unexpected(CodeEditError::HandleLimit);
  auto generation = reuse ? acquire_generation() : storage->scope;
  if (generation == 0)
    return std::unexpected(CodeEditError::HandleLimit);

  // Reserve first. Once the operand has been installed, its owner can be
  // linked into the order without another allocation.
  reserve_additional(storage->order, 1);
  if (!reuse)
    reserve_additional(storage->slots, 1);
  auto instruction = build(value);
  if (!instruction)
    return std::unexpected(instruction.error());

  if (!reuse)
    index = static_cast<std::uint32_t>(storage->slots.size());
  const InstructionId id{index, generation};
  storage->install_instruction(id, *instruction);
  storage->order.insert(storage->order.begin() + static_cast<std::ptrdiff_t>(position), index);
  ++storage->revision;
  return id;
}

std::expected<void, CodeEditError> MethodCode::replace(InstructionId id, const InstructionBuilder& value) {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidInstruction);
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);
  // Stage the stamp node before build() can publish an owned switch.
  decltype(storage->changes) new_stamps(storage->resource);
  const bool owns_switch = std::holds_alternative<TableSwitch>(value) || std::holds_alternative<LookupSwitch>(value);
  if (owns_switch && !storage->changes.contains(id.index))
    new_stamps.emplace(id.index, storage->revision + 1);
  auto replacement = build(value);
  if (!replacement)
    return std::unexpected(replacement.error());

  auto& instruction = storage->slots[id.index].instruction;
  if (instruction == *replacement)
    return {};
  storage->changes.merge(new_stamps);
  storage->changes.insert_or_assign(id.index, storage->revision + 1);
  release_operand(instruction);
  instruction = *replacement;
  ++storage->revision;
  return {};
}

std::expected<void, CodeEditError> MethodCode::erase(InstructionId id) {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidInstruction);
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);
  if (std::ranges::find(storage->labels, id) != storage->labels.end())
    return std::unexpected(CodeEditError::LabeledInstruction);

  auto at = std::ranges::find(storage->order, id.index);
  storage->order.erase(at);
  auto& slot = storage->slots[id.index];
  release_operand(slot.instruction);
  slot = InstructionSlot{Instruction{}, 0, storage->free_instruction};
  storage->free_instruction = id.index;
  storage->changes.erase(id.index);
  ++storage->revision;
  return {};
}

std::expected<void, CodeEditError> MethodCode::move_before(InstructionId id, CodePosition position) {
  if (!storage->contains(id) || (position.before && !storage->contains(*position.before)))
    return std::unexpected(CodeEditError::InvalidInstruction);
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);

  auto first = std::ranges::find(storage->order, id.index);
  auto boundary = position.before ? std::ranges::find(storage->order, position.before->index) : storage->order.end();
  if (boundary == first || boundary == first + 1)
    return {};
  if (first < boundary)
    std::rotate(first, first + 1, boundary);
  else
    std::rotate(boundary, first, first + 1);
  ++storage->revision;
  return {};
}

std::expected<LabelId, CodeEditError> MethodCode::add_label(std::optional<CodePosition> position) {
  if (!storage->can_edit() || storage->labels.size() >= NO_SLOT)
    return std::unexpected(CodeEditError::HandleLimit);
  if (position && position->before && !storage->contains(*position->before))
    return std::unexpected(CodeEditError::InvalidInstruction);

  auto id = LabelId{static_cast<std::uint32_t>(storage->labels.size()), storage->scope};
  reserve_additional(storage->labels, 1);
  reserve_additional(storage->label_order, 1);
  storage->labels.push_back(position ? position->before.value_or(EXCLUSIVE_END) : UNBOUND);
  storage->label_order.push_back(id.value);
  return id;
}

std::expected<LabelId, CodeEditError> MethodCode::make_label() {
  auto label = add_label(std::nullopt);
  if (label)
    ++storage->revision;
  return label;
}

std::expected<LabelId, CodeEditError> MethodCode::make_label(CodePosition position) {
  auto label = add_label(position);
  if (label)
    ++storage->revision;
  return label;
}

std::expected<void, CodeEditError> MethodCode::bind_label(LabelId id, CodePosition position) {
  return set_label_binding(id, position, true);
}

std::expected<void, CodeEditError> MethodCode::set_label_binding(LabelId id, CodePosition position, bool record_edit) {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidLabel);
  if (position.before && !storage->contains(*position.before))
    return std::unexpected(CodeEditError::InvalidInstruction);
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);
  auto next = position.before.value_or(EXCLUSIVE_END);
  if (storage->labels[id.value] != next) {
    if (record_edit) {
      storage->changes.insert_or_assign(binding_key(id), storage->revision + 1);
      const auto old_order = std::ranges::find(storage->label_order, id.value);
      std::rotate(old_order, old_order + 1, storage->label_order.end());
    }
    storage->labels[id.value] = next;
    ++storage->revision;
  }
  return {};
}

std::expected<CodePosition, CodeEditError> MethodCode::label_position(LabelId id) const {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidLabel);
  auto target = storage->labels[id.value];
  if (target == UNBOUND)
    return std::unexpected(CodeEditError::UnboundLabel);
  return CodePosition{target == EXCLUSIVE_END ? std::nullopt : std::optional(target)};
}

std::expected<LabelId, CodeEditError> MethodCode::branch_target(InstructionId id) const {
  auto instruction = get(id);
  if (!instruction)
    return std::unexpected(instruction.error());
  if (opcode_info(instruction->opcode()).form != OperandForm::BranchShort)
    return std::unexpected(CodeEditError::WrongOperandForm);
  return LabelId{instruction->operand_bits, storage->scope};
}

std::expected<SwitchId, CodeEditError> MethodCode::switch_id(InstructionId id) const {
  auto instruction = get(id);
  if (!instruction)
    return std::unexpected(instruction.error());
  if (instruction->opcode() != Opcode::TableSwitch && instruction->opcode() != Opcode::LookupSwitch)
    return std::unexpected(CodeEditError::WrongOperandForm);
  return SwitchId{instruction->operand_bits, storage->scope};
}

std::expected<TableSwitch, CodeEditError> MethodCode::table_switch(SwitchId id) const {
  auto slot = storage->find_switch(id);
  if (slot) {
    if (auto* table = std::get_if<OwnedTableSwitch>(&slot->payload))
      return TableSwitch{table->low, table->default_target, table->targets};
  }
  return std::unexpected(CodeEditError::InvalidSwitch);
}

std::expected<LookupSwitch, CodeEditError> MethodCode::lookup_switch(SwitchId id) const {
  auto slot = storage->find_switch(id);
  if (slot) {
    if (auto* lookup = std::get_if<OwnedLookupSwitch>(&slot->payload))
      return LookupSwitch{lookup->default_target, lookup->pairs};
  }
  return std::unexpected(CodeEditError::InvalidSwitch);
}

std::expected<Instruction, CodeEditError> MethodCode::build(const InstructionBuilder& value) {
  return build(value, [this](auto view) { return storage->add_switch(view); }, [this](LabelId id) -> std::optional<CodeEditError> {
                 if (!storage->contains(id))
                   return CodeEditError::InvalidLabel;
                 return std::nullopt; });
}

template<typename AddSwitch, typename CheckLabel>
std::expected<Instruction, CodeEditError> MethodCode::build(const InstructionBuilder& value, AddSwitch&& add_switch,
                                                            CheckLabel&& check_label) {
  if (auto instruction = std::get_if<Instruction>(&value)) {
    auto op = instruction->opcode();
    const auto& info = opcode_info(op);
    if (info.logical_opcode != op || info.form == OperandForm::Illegal || info.form == OperandForm::BranchShort ||
        info.form == OperandForm::TableSwitch || info.form == OperandForm::LookupSwitch || info.form == OperandForm::WidePrefix)
      return std::unexpected(CodeEditError::WrongOperandForm);
    return *instruction;
  }
  if (auto branch = std::get_if<Branch>(&value)) {
    if (opcode_info(branch->opcode).form != OperandForm::BranchShort)
      return std::unexpected(CodeEditError::WrongOperandForm);
    if (auto error = check_label(branch->target))
      return std::unexpected(*error);
    return Instruction(branch->opcode, branch->target.value, 0, branch->encoding_hint);
  }

  if (auto table = std::get_if<TableSwitch>(&value)) {
    if (table->targets.empty() || table->targets.size() > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::int64_t>(table->low) + static_cast<std::int64_t>(table->targets.size()) - 1 > std::numeric_limits<std::int32_t>::max())
      return std::unexpected(CodeEditError::InvalidSwitch);
    if (auto error = check_label(table->default_target))
      return std::unexpected(*error);
    for (const auto target : table->targets)
      if (auto error = check_label(target))
        return std::unexpected(*error);
    return add_switch(*table).transform([](auto id) { return Instruction(Opcode::TableSwitch, id); });
  }

  const auto* lookup = std::get_if<LookupSwitch>(&value);
  if (!lookup)
    return std::unexpected(CodeEditError::WrongOperandForm);
  if (lookup->pairs.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    return std::unexpected(CodeEditError::InvalidSwitch);
  if (auto error = check_label(lookup->default_target))
    return std::unexpected(*error);
  for (std::size_t i = 0; i < lookup->pairs.size(); ++i) {
    if (i != 0 && lookup->pairs[i - 1].key >= lookup->pairs[i].key)
      return std::unexpected(CodeEditError::InvalidSwitch);
    if (auto error = check_label(lookup->pairs[i].target))
      return std::unexpected(*error);
  }
  return add_switch(*lookup).transform([](auto id) { return Instruction(Opcode::LookupSwitch, id); });
}

std::expected<std::vector<InstructionId>, BlockEditError> MethodCode::insert_block_before(
    InstructionId at, std::span<const InstructionBuilder> block) {
  if (!storage->contains(at))
    return std::unexpected(BlockEditError{CodeEditError::InvalidInstruction, std::nullopt});
  const auto anchor = std::ranges::find(storage->order, at.index);
  return insert_block(static_cast<std::size_t>(anchor - storage->order.begin()), block);
}

std::expected<std::vector<InstructionId>, BlockEditError> MethodCode::insert_block_at(
    LabelId at, std::span<const InstructionBuilder> block) {
  const auto position = label_position(at);
  if (!position)
    return std::unexpected(BlockEditError{position.error(), std::nullopt});

  if (block.empty())
    return std::vector<InstructionId>{};
  EditStamps staged(storage->resource);
  storage->stage_stamp(staged, binding_key(at));
  auto inserted = position->before ? insert_block_before(*position->before, block) : insert_block(size(), block);
  if (inserted) {
    storage->changes.merge(staged);
    storage->publish_binding(at, inserted->front(), storage->revision);
  }
  return inserted;
}

std::expected<std::vector<InstructionId>, BlockEditError> MethodCode::insert_block(
    std::size_t position, std::span<const InstructionBuilder> block) {
  return splice(position, block, {}, nullptr);
}

std::expected<std::vector<InstructionId>, BlockEditError> MethodCode::splice(
    std::size_t position, std::span<const InstructionBuilder> block, std::span<const InstructionId> removed,
    const detail::CodePatch* patch) {
  const auto binds = patch ? patch->label_bindings : std::span<const std::optional<std::size_t>>{};
  const auto binding_order = patch ? patch->binding_order : std::span<const std::size_t>{};
  if (block.empty() && removed.empty() && binding_order.empty())
    return std::vector<InstructionId>{};
  if (!storage->can_edit())
    return std::unexpected(BlockEditError{CodeEditError::HandleLimit, block.empty() ? std::nullopt : std::optional<std::size_t>{0}});
  const bool retain_entry = !block.empty() && !removed.empty();
  const auto erased = removed.subspan(retain_entry ? 1 : 0);
  if (!erased.empty() && !storage->labels.empty()) {
    std::vector<std::uint32_t> slots;
    slots.reserve(erased.size());
    for (auto id : erased)
      slots.push_back(id.index);
    std::ranges::sort(slots);
    for (auto target : storage->labels)
      if (target.generation != 0 && std::ranges::binary_search(slots, target.index))
        return std::unexpected(BlockEditError{CodeEditError::LabeledInstruction, std::nullopt});
  }

  const auto successor = at(position + removed.size());
  const auto fixups = patch ? patch->branch_fixups : std::span<const std::pair<std::size_t, std::size_t>>{};
  std::vector<LabelId> local_labels(binds.size());
  std::vector<std::size_t> target_boundaries;
  auto next_fixup = fixups.begin();
  const auto new_label = [&]() -> std::expected<LabelId, CodeEditError> {
    if (target_boundaries.size() >= NO_SLOT - storage->labels.size())
      return std::unexpected(CodeEditError::HandleLimit);
    return LabelId{static_cast<std::uint32_t>(storage->labels.size() + target_boundaries.size()), storage->scope};
  };
  for (const auto ordinal : binding_order) {
    if (ordinal >= binds.size())
      return std::unexpected(BlockEditError{CodeEditError::InvalidLabel, std::nullopt});
    const auto boundary = binds[ordinal];
    if (!boundary || *boundary > block.size())
      return std::unexpected(BlockEditError{CodeEditError::InvalidLabel, std::nullopt});
    const auto id = new_label();
    if (!id)
      return std::unexpected(BlockEditError{id.error(), std::nullopt});
    local_labels[ordinal] = *id;
    target_boundaries.push_back(*boundary);
  }

  std::vector<LabelId> moved_labels;
  auto label_insert = storage->label_order.size();
  // Preceding labels follow the patch entry. Trailing builder labels go ahead of the first label after the patch.
  const auto label_anchor = patch ? patch->label_anchor : std::nullopt;
  const bool before_anchor = patch && patch->placement == detail::PatchPlacement::Before;
  bool before_insertion = label_anchor || (patch && patch->ordered && before_anchor);
  if (before_insertion || !target_boundaries.empty()) {
    const auto target = successor.generation == 0 ? EXCLUSIVE_END : successor;
    for (std::size_t i = 0; i < storage->label_order.size(); ++i) {
      const auto label = storage->label_order[i];
      if (storage->labels[label] != target)
        continue;
      const auto id = LabelId{label, storage->scope};
      if (label_anchor == id && before_anchor)
        before_insertion = false;
      if (!before_insertion) {
        label_insert = i;
        break;
      }
      if (!block.empty())
        moved_labels.push_back(id);
      if (label_anchor == id)
        before_insertion = false;
    }
  }
  if (patch && patch->move_label && !block.empty())
    moved_labels.push_back(*patch->move_label);

  const auto resolve_local = [&](std::size_t label) -> std::expected<LabelId, CodeEditError> {
    if (label >= binds.size())
      return std::unexpected(CodeEditError::InvalidLabel);
    if (!binds[label] || local_labels[label].scope == 0)
      return std::unexpected(CodeEditError::UnboundLabel);
    const auto boundary = *binds[label];
    if (boundary > block.size() || (boundary == block.size() && successor.generation == 0))
      return std::unexpected(CodeEditError::InvalidLabel);
    return local_labels[label];
  };
  const auto check_label = [&](LabelId id) -> std::optional<CodeEditError> {
    if (!storage->contains(id))
      return CodeEditError::InvalidLabel;
    if (patch) {
      const auto destination = storage->labels[id.value];
      if (destination == UNBOUND)
        return CodeEditError::UnboundLabel;
      if (destination == EXCLUSIVE_END && std::ranges::find(moved_labels, id) == moved_labels.end())
        return CodeEditError::InvalidLabel;
    }
    return std::nullopt;
  };
  const auto limit = std::min(static_cast<std::size_t>(NO_SLOT), storage->order.max_size());
  if (block.size() > limit - (size() - removed.size()))
    return std::unexpected(BlockEditError{CodeEditError::HandleLimit, limit - (size() - removed.size())});

  // Plan free-list reuse and copy switch data before mutating any live slots.
  std::vector<InstructionId> inserted;
  inserted.reserve(block.size());
  std::pmr::vector<Instruction> instructions(storage->resource);
  instructions.reserve(block.size());
  std::pmr::vector<SwitchPayload> payloads(storage->resource);
  auto next_instruction = storage->free_instruction;
  auto next_switch = storage->free_switch;
  std::size_t new_instructions = 0;
  std::size_t new_switches = 0;
  auto prepare_switch = [&](auto view) -> std::expected<std::uint32_t, CodeEditError> {
    std::uint32_t id;
    if (next_switch == NO_SLOT) {
      if (storage->switches.size() + new_switches == SWITCH_SLOT_COUNT)
        return std::unexpected(CodeEditError::HandleLimit);
      const auto index = static_cast<std::uint32_t>(storage->switches.size() + new_switches);
      ++new_switches;
      id = switch_value(index, 1);
    } else {
      const auto& slot = storage->switches[next_switch];
      id = switch_value(next_switch, slot.generation);
      next_switch = slot.next_free;
    }
    payloads.push_back(own_switch(view, storage->resource));
    return id;
  };
  for (std::size_t i = 0; i < block.size(); ++i) {
    auto instruction = [&]() -> std::expected<Instruction, CodeEditError> {
      if (next_fixup == fixups.end() || next_fixup->first != i)
        return build(block[i], prepare_switch, check_label);
      const auto first = next_fixup;
      while (next_fixup != fixups.end() && next_fixup->first == i)
        ++next_fixup;
      return build_local_targets(block[i], {first, next_fixup}, resolve_local, [&](const InstructionBuilder& resolved) {
        return build(resolved, prepare_switch, [](LabelId) -> std::optional<CodeEditError> { return std::nullopt; });
      });
    }();
    if (!instruction)
      return std::unexpected(BlockEditError{instruction.error(), i});
    instructions.push_back(*instruction);
    if (retain_entry && i == 0) {
      inserted.push_back(removed.front());
    } else if (next_instruction == NO_SLOT) {
      if (storage->slots.size() + new_instructions >= std::min(storage->slots.max_size(), static_cast<std::size_t>(NO_SLOT)))
        return std::unexpected(BlockEditError{CodeEditError::HandleLimit, i});
      const auto index = static_cast<std::uint32_t>(storage->slots.size() + new_instructions);
      ++new_instructions;
      inserted.push_back({index, storage->scope});
    } else {
      inserted.push_back({next_instruction, 0});
      next_instruction = storage->slots[next_instruction].next_free;
    }
  }
  if (next_fixup != fixups.end())
    return std::unexpected(BlockEditError{CodeEditError::InvalidLabel, next_fixup->first});
  if (retain_entry && !patch->match_position && block.size() == 1 &&
      storage->slots[removed.front().index].instruction == instructions.front())
    return inserted;

  std::vector<std::uint32_t> incoming;
  if (patch && patch->include_incoming && !block.empty()) {
    for (const auto slot : storage->order)
      if (redirect_targets(slot, patch->anchor, std::nullopt))
        incoming.push_back(slot);
  }
  std::optional<LabelId> entry;
  if (!incoming.empty()) {
    auto id = new_label();
    if (!id)
      return std::unexpected(BlockEditError{id.error(), std::nullopt});
    entry = *id;
    target_boundaries.push_back(0);
  }
  std::vector<std::uint32_t> trailing_labels;
  for (std::size_t i = 0; i < target_boundaries.size(); ++i)
    if (target_boundaries[i] == block.size())
      trailing_labels.push_back(static_cast<std::uint32_t>(storage->labels.size() + i));
  if (block.size() > removed.size())
    reserve_additional(storage->order, block.size() - removed.size());
  reserve_additional(storage->slots, new_instructions);
  reserve_additional(storage->switches, new_switches);
  reserve_additional(storage->labels, target_boundaries.size());
  reserve_additional(storage->label_order, target_boundaries.size());
  decltype(storage->changes) new_stamps(storage->resource);
  if (retain_entry)
    storage->stage_stamp(new_stamps, removed.front().index);
  for (const auto slot : incoming)
    storage->stage_stamp(new_stamps, slot);
  for (const auto label : moved_labels)
    storage->stage_stamp(new_stamps, binding_key(label));
  for (std::size_t i = 0; i < inserted.size(); ++i) {
    auto& id = inserted[i];
    if (id.generation == 0) {
      id.generation = acquire_generation();
      if (id.generation == 0)
        return std::unexpected(BlockEditError{CodeEditError::HandleLimit, i});
    }
  }

  // No allocations or validation below this point. Redirect only pre-existing edges.
  storage->changes.merge(new_stamps);
  for (const auto slot : incoming) {
    redirect_targets(slot, patch->anchor, entry);
    storage->changes.at(slot) = storage->revision + 1;
  }
  const auto trailing = successor.generation == 0 ? EXCLUSIVE_END : successor;
  for (const auto boundary : target_boundaries) {
    if (boundary != block.size())
      storage->label_order.push_back(static_cast<std::uint32_t>(storage->labels.size()));
    storage->labels.push_back(boundary == block.size() ? trailing : inserted[boundary]);
  }
  storage->label_order.insert(storage->label_order.begin() + static_cast<std::ptrdiff_t>(label_insert),
                              trailing_labels.begin(), trailing_labels.end());
  for (const auto label : moved_labels)
    storage->publish_binding(label, inserted.front(), storage->revision + 1);

  // Install allocated switch/instruction slots before adding the newly erased slots to their free lists.
  for (auto& payload : payloads)
    storage->install_switch(std::move(payload));
  for (std::size_t i = retain_entry ? 1 : 0; i < inserted.size(); ++i)
    storage->install_instruction(inserted[i], instructions[i]);
  if (retain_entry) {
    auto& first = storage->slots[removed.front().index].instruction;
    release_operand(first);
    first = instructions.front();
    storage->changes.at(removed.front().index) = storage->revision + 1;
  }
  for (auto id : erased) {
    auto& slot = storage->slots[id.index];
    release_operand(slot.instruction);
    slot = {Instruction{}, 0, storage->free_instruction};
    storage->free_instruction = id.index;
    storage->changes.erase(id.index);
  }
  auto order = storage->order.begin() + static_cast<std::ptrdiff_t>(position);
  if (block.size() > removed.size())
    order = storage->order.insert(order, block.size() - removed.size(), 0);
  else if (removed.size() > block.size())
    order = storage->order.erase(order, order + static_cast<std::ptrdiff_t>(removed.size() - block.size()));
  for (auto id : inserted)
    *order++ = id.index;
  ++storage->revision;
  return inserted;
}

void MethodCode::release_operand(Instruction value) {
  if (value.opcode() != Opcode::TableSwitch && value.opcode() != Opcode::LookupSwitch)
    return;
  auto index = switch_index(value.operand_bits);
  auto& slot = storage->switches[index];
  slot.payload.emplace<std::monostate>();
  slot.padding_length = 0;
  if (slot.generation != std::numeric_limits<std::uint16_t>::max()) {
    ++slot.generation;
    slot.next_free = storage->free_switch;
    storage->free_switch = index;
  }
}

bool MethodCode::redirect_targets(std::uint32_t index, InstructionId anchor, std::optional<LabelId> entry) {
  auto& instruction = storage->slots[index].instruction;
  bool changed = false;
  const auto redirect = [&](LabelId& target) {
    if (storage->labels[target.value] == anchor) {
      changed = true;
      if (entry)
        target = *entry;
    }
  };
  switch (opcode_info(instruction.opcode()).form) {
    case OperandForm::BranchShort: {
      auto target = LabelId{instruction.operand_bits, storage->scope};
      redirect(target);
      if (changed && entry)
        instruction.operand_bits = target.value;
      break;
    }
    case OperandForm::TableSwitch: {
      auto& table = std::get<OwnedTableSwitch>(storage->switches[switch_index(instruction.operand_bits)].payload);
      redirect(table.default_target);
      for (auto& target : table.targets)
        redirect(target);
      break;
    }
    case OperandForm::LookupSwitch: {
      auto& lookup = std::get<OwnedLookupSwitch>(storage->switches[switch_index(instruction.operand_bits)].payload);
      redirect(lookup.default_target);
      for (auto& pair : lookup.pairs)
        redirect(pair.target);
      break;
    }
    default:
      break;
  }
  return changed;
}

bool MethodCode::changed_since(std::uint32_t slot, std::uint64_t revision) const {
  const auto& changes = storage->changes;
  if (storage->revision == revision || changes.empty())
    return false;
  const auto changed = [&](std::uint64_t key) {
    const auto stamp = changes.find(key);
    return stamp != changes.end() && stamp->second > revision;
  };
  if (changed(slot))
    return true;
  if (changes.rbegin()->first < LABEL_KEY_BASE)
    return false;
  const auto rebound = [&](LabelId id) { return changed(binding_key(id)); };
  const auto& instruction = storage->slots[slot].instruction;
  switch (opcode_info(instruction.opcode()).form) {
    case OperandForm::BranchShort:
      return rebound(LabelId{instruction.operand_bits, storage->scope});
    case OperandForm::TableSwitch: {
      const auto& table = std::get<OwnedTableSwitch>(storage->switches[switch_index(instruction.operand_bits)].payload);
      return rebound(table.default_target) || std::ranges::any_of(table.targets, rebound);
    }
    case OperandForm::LookupSwitch: {
      const auto& lookup = std::get<OwnedLookupSwitch>(storage->switches[switch_index(instruction.operand_bits)].payload);
      return rebound(lookup.default_target) || std::ranges::any_of(lookup.pairs, rebound, &LookupPair::target);
    }
    default:
      return false;
  }
}

namespace detail {

std::expected<std::size_t, BlockEditError> BytecodeAccess::match_position(
    const MethodCode& code, std::span<const InstructionId> ids, std::uint32_t scope, std::uint64_t revision) {
  const auto stale = std::unexpected(BlockEditError{CodeEditError::StaleMatch, std::nullopt});
  const auto& state = *code.storage;
  if (ids.empty() || scope == 0 || scope != state.scope || revision > state.revision || !state.contains(ids.front()))
    return stale;
  const auto at = std::ranges::find(state.order, ids.front().index);
  const auto position = static_cast<std::size_t>(at - state.order.begin());
  if (ids.size() > code.size() - position)
    return stale;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto id = ids[i];
    if (code.at(position + i) != id || code.changed_since(id.index, revision))
      return stale;
  }
  return position;
}

std::expected<std::vector<InstructionId>, BlockEditError> BytecodeAccess::apply_patch(
    MethodCode& code, const CodePatch& patch, std::span<const InstructionBuilder> block) {
  const auto fail = [](CodeEditError error) { return std::unexpected(BlockEditError{error, std::nullopt}); };
  const auto& state = *code.storage;
  if (patch.include_incoming && patch.placement != PatchPlacement::Before)
    return fail(CodeEditError::InvalidOperand);
  std::size_t position = patch.match_position.value_or(code.size());
  if (patch.label_anchor) {
    const auto binding = code.label_position(*patch.label_anchor);
    if (!binding)
      return fail(binding.error());
    if (binding->before) {
      const auto at = std::ranges::find(state.order, binding->before->index);
      position = static_cast<std::size_t>(at - state.order.begin());
    }
  } else if (!patch.match_position && patch.placement != PatchPlacement::Append) {
    if (!state.contains(patch.anchor))
      return fail(CodeEditError::InvalidInstruction);
    const auto at = std::ranges::find(state.order, patch.anchor.index);
    position = static_cast<std::size_t>(at - state.order.begin());
    if (patch.placement == PatchPlacement::After)
      ++position;
  }
  if (patch.move_label) {
    const auto binding = code.label_position(*patch.move_label);
    if (!binding)
      return fail(binding.error());
    if (binding->before.value_or(InstructionId{}) != code.at(position))
      return fail(CodeEditError::InvalidLabel);
  }
  std::span<const InstructionId> removed;
  if (patch.placement == PatchPlacement::Replace)
    removed = patch.match_position ? patch.matched_ids : std::span<const InstructionId>(&patch.anchor, 1);
  return code.splice(position, block, removed, &patch);
}

std::uint32_t BytecodeAccess::scope(const MethodCode& code) {
  return code.storage->scope;
}

std::size_t BytecodeAccess::slot_count(const MethodCode& code) {
  return code.storage->slots.size();
}

std::size_t BytecodeAccess::label_count(const MethodCode& code) {
  return code.storage->labels.size();
}

std::expected<LabelId, CodeEditError> BytecodeAccess::original_label(MethodCode& code, CodePosition position) {
  return code.add_label(position);
}

void BytecodeAccess::mark_clean(MethodCode& code) {
  code.storage->revision = 0;
  code.storage->changes.clear();
}

void BytecodeAccess::set_original_switch_padding(MethodCode& code, InstructionId id, std::span<const std::byte> padding) {
  auto payload = code.switch_id(id).value();
  auto& slot = code.storage->switches[switch_index(payload.value)];
  assert(padding.size() <= slot.original_padding.size());
  std::ranges::copy(padding, slot.original_padding.begin());
  slot.padding_length = static_cast<std::uint8_t>(padding.size());
}

std::span<const std::byte> BytecodeAccess::original_switch_padding(const MethodCode& code, InstructionId id) {
  auto payload = code.switch_id(id).value();
  const auto& slot = code.storage->switches[switch_index(payload.value)];
  return std::span(slot.original_padding).first(slot.padding_length);
}

} // namespace detail
} // namespace cafe
