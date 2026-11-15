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
#include <stdexcept>
#include <vector>

#include "code_access.hpp"

namespace cafe {
namespace {

constexpr std::uint32_t NO_SLOT = std::numeric_limits<std::uint32_t>::max(); ///< Free-list sentinel indicating that no reusable slot exists.
constexpr std::uint32_t SWITCH_SLOT_COUNT = 1u << 16; ///< Number of operand slots representable by the packed switch handle.
constexpr InstructionId UNBOUND{}; ///< Label-table sentinel for a label with no boundary binding.
constexpr InstructionId EXCLUSIVE_END{NO_SLOT, 0}; ///< Label-table sentinel identifying the boundary after the last instruction.

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
      resource(resource), slots(resource), order(resource), labels(resource), switches(resource) {
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
  std::pmr::vector<SwitchSlot> switches; ///< Out-of-line switch operand slots with generation/free-list metadata.
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
  auto inserted = position->before ? insert_before(*position->before, value) : append(value);
  if (inserted)
    storage->labels[id.value] = *inserted;
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
  auto replacement = build(value);
  if (!replacement)
    return std::unexpected(replacement.error());

  auto& instruction = storage->slots[id.index].instruction;
  if (instruction == *replacement)
    return {};
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
  storage->labels.push_back(position ? position->before.value_or(EXCLUSIVE_END) : UNBOUND);
  return id;
}

std::expected<LabelId, CodeEditError> MethodCode::make_label() {
  auto label = add_label(std::nullopt);
  if (label)
    ++storage->revision;
  return label;
}

std::expected<void, CodeEditError> MethodCode::bind_label(LabelId id, CodePosition position) {
  if (!storage->contains(id))
    return std::unexpected(CodeEditError::InvalidLabel);
  if (position.before && !storage->contains(*position.before))
    return std::unexpected(CodeEditError::InvalidInstruction);
  if (!storage->can_edit())
    return std::unexpected(CodeEditError::HandleLimit);
  auto next = position.before.value_or(EXCLUSIVE_END);
  if (storage->labels[id.value] != next) {
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
  return build(value, [this](auto view) { return storage->add_switch(view); });
}

template<typename AddSwitch>
std::expected<Instruction, CodeEditError> MethodCode::build(const InstructionBuilder& value, AddSwitch&& add_switch) {
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
    if (!storage->contains(branch->target))
      return std::unexpected(CodeEditError::InvalidLabel);
    return Instruction(branch->opcode, branch->target.value, 0, branch->encoding_hint);
  }

  auto missing = [&](LabelId id) { return !storage->contains(id); };
  if (auto table = std::get_if<TableSwitch>(&value)) {
    if (table->targets.empty() || table->targets.size() > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::int64_t>(table->low) + static_cast<std::int64_t>(table->targets.size()) - 1 > std::numeric_limits<std::int32_t>::max())
      return std::unexpected(CodeEditError::InvalidSwitch);
    if (missing(table->default_target) || std::ranges::any_of(table->targets, missing))
      return std::unexpected(CodeEditError::InvalidLabel);
    return add_switch(*table).transform([](auto id) { return Instruction(Opcode::TableSwitch, id); });
  }

  const auto* lookup = std::get_if<LookupSwitch>(&value);
  if (!lookup)
    return std::unexpected(CodeEditError::WrongOperandForm);
  if (lookup->pairs.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    return std::unexpected(CodeEditError::InvalidSwitch);
  if (missing(lookup->default_target))
    return std::unexpected(CodeEditError::InvalidLabel);
  for (std::size_t i = 0; i < lookup->pairs.size(); ++i) {
    if (i != 0 && lookup->pairs[i - 1].key >= lookup->pairs[i].key)
      return std::unexpected(CodeEditError::InvalidSwitch);
    if (missing(lookup->pairs[i].target))
      return std::unexpected(CodeEditError::InvalidLabel);
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

  auto inserted = position->before ? insert_block_before(*position->before, block) : insert_block(size(), block);
  if (inserted && !inserted->empty())
    storage->labels[at.value] = inserted->front();
  return inserted;
}

std::expected<std::vector<InstructionId>, BlockEditError> MethodCode::insert_block(
    std::size_t position, std::span<const InstructionBuilder> block) {
  if (block.empty())
    return std::vector<InstructionId>{};
  if (!storage->can_edit())
    return std::unexpected(BlockEditError{CodeEditError::HandleLimit, 0});
  const auto limit = std::min({static_cast<std::size_t>(NO_SLOT), storage->slots.max_size(), storage->order.max_size()});
  if (block.size() > limit - size())
    return std::unexpected(BlockEditError{CodeEditError::HandleLimit, limit - size()});

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
    const auto instruction = build(block[i], prepare_switch);
    if (!instruction)
      return std::unexpected(BlockEditError{instruction.error(), i});
    instructions.push_back(*instruction);
    if (next_instruction == NO_SLOT) {
      const auto index = static_cast<std::uint32_t>(storage->slots.size() + new_instructions);
      ++new_instructions;
      inserted.push_back({index, storage->scope});
    } else {
      inserted.push_back({next_instruction, 0});
      next_instruction = storage->slots[next_instruction].next_free;
    }
  }

  reserve_additional(storage->order, block.size());
  reserve_additional(storage->slots, new_instructions);
  reserve_additional(storage->switches, new_switches);
  for (std::size_t i = 0; i < inserted.size(); ++i) {
    auto& id = inserted[i];
    if (id.generation == 0) {
      id.generation = acquire_generation();
      if (id.generation == 0)
        return std::unexpected(BlockEditError{CodeEditError::HandleLimit, i});
    }
  }

  // Capacity and handles are ready. Commit with one shift of the order vector.
  auto order = storage->order.insert(storage->order.begin() + static_cast<std::ptrdiff_t>(position), block.size(), 0);
  for (auto& payload : payloads)
    storage->install_switch(std::move(payload));
  for (std::size_t i = 0; i < inserted.size(); ++i) {
    storage->install_instruction(inserted[i], instructions[i]);
    *order++ = inserted[i].index;
  }
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

namespace detail {

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
