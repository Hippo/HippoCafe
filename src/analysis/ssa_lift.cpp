/**
 * @file
 * @brief Symbolic execution of verified bytecode into per-method SSA.
 * @ingroup implementation
 */
#include <cafe/analysis/ssa.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

#include "analysis_detail.hpp"
#include "instruction_effects.hpp"
#include "io/ssa_relocation.hpp"
#include "ssa_detail.hpp"
#include "ssa_frames.hpp"

namespace cafe::analysis {
namespace {

constexpr std::uint32_t STACK_BASE = 65536; ///< Stack ordinals follow all possible local slots.
constexpr std::uint64_t NO_PORT = UINT64_MAX; ///< Unusable verifier slot without a definition.

/// Pack the two independent coordinates of a sparse frame cell.
/// @param index Block or edge table index.
/// @param cell Local slot or biased stack ordinal.
/// @return A key preserving both 32-bit coordinates.
constexpr std::uint64_t port(std::uint32_t index, std::uint32_t cell) {
  return (static_cast<std::uint64_t>(index) << 32) | cell;
}

/// A value is either already defined, or refers lazily to one of a block's incoming frame cells.
struct Symbol {
  SsaValueId value{}; ///< Resolved definition if this symbol is concrete.
  std::uint64_t input = NO_PORT; ///< Source block and frame cell for a deferred definition.
};

/// A write takes effect after its original instruction. All handler rows share this history.
struct LocalWrite {
  std::uint32_t position; ///< Zero-based instruction position of the assignment.
  Symbol symbol; ///< Value visible on successful completion.
};

/// Check whether a frame position can carry an SSA definition.
/// @param type Verification type at the frame position.
/// @return True for values with usable JVM identities.
bool usable(VerificationType type) {
  return type.kind != VerificationKind::Top && type.kind != VerificationKind::WideContinuation;
}

/// Keep the underlying verifier failure when lifting fails.
/// @param error Verifier failure including its original location.
/// @return An SSA diagnostic carrying the verifier cause.
SsaError from_analysis(AnalysisError error) {
  auto kind = SsaErrorKind::AnalysisFailure;
  if (error.kind == AnalysisErrorKind::Cancelled)
    kind = SsaErrorKind::Cancelled;
  if (error.kind == AnalysisErrorKind::ResourceLimitExceeded)
    kind = SsaErrorKind::ResourceLimitExceeded;
  return {.kind = kind, .detail = error.detail, .analysis = std::move(error)};
}

/// Translate a normal CFG transfer to its SSA edge kind.
/// @param kind Verified normal edge classification.
/// @return Corresponding SSA edge classification.
SsaEdgeKind edge_kind(EdgeKind kind) {
  switch (kind) {
    case EdgeKind::Fallthrough:
      return SsaEdgeKind::Fallthrough;
    case EdgeKind::Conditional:
      return SsaEdgeKind::Conditional;
    case EdgeKind::Unconditional:
      return SsaEdgeKind::Unconditional;
    case EdgeKind::SwitchCase:
      return SsaEdgeKind::SwitchCase;
    case EdgeKind::SwitchDefault:
      return SsaEdgeKind::SwitchDefault;
  }
  return SsaEdgeKind::Unconditional;
}

/// Recognize normalized JVM local reads.
/// @param opcode Logical JVM opcode.
/// @return True for the five local load operations.
bool is_load(Opcode opcode) {
  return opcode >= Opcode::ILoad && opcode <= Opcode::ALoad;
}

/// Recognize normalized JVM local writes.
/// @param opcode Logical JVM opcode.
/// @return True for the five local store operations.
bool is_store(Opcode opcode) {
  return opcode >= Opcode::IStore && opcode <= Opcode::AStore;
}

/// Construct all CFG edges before sealing incoming definitions. Only frame cells that flow into
/// an operation (or debug assignment) cause phi construction. Symbol resolution uses an explicit
/// pending list so arbitrarily deep loops and branch chains cannot exhaust the native stack.
class Lifter {
public:
  /// Bind construction to a verified source body and an empty SSA snapshot.
  /// @param owner Class owning the method and constants.
  /// @param body Decoded source method body.
  /// @param graph Destination graph carrying method parameter definitions.
  /// @param cfg Control flow split at declared verifier frames.
  /// @param frames Verified frame for each CFG block.
  Lifter(const ClassFile& owner, const CodeAttribute& body, SsaMethod& graph,
         const ControlFlowGraph& cfg, const SsaFrames& frames) :
      owner(owner), code(body.code()), graph(graph), storage(SsaAccess::get(graph)), cfg(cfg), frames(frames),
      resource(&storage.budget), remaining(storage.limits.max_work), origin(resource), position(resource),
      allocation_sites(resource), roots(resource), changes(resource), exits(resource), entry_offsets(resource), phis(resource), pending(resource) {
    // Source local assignments also represent debugger-visible uses where local ranges are present.
    keep_locals = ::cafe::detail::ssa_has_local_metadata(owner, body);
  }

  /// Lift the verified method and seal its phi arguments.
  /// @param simulator Prepared instruction transfers for the source method.
  /// @param seeds Declared stack-map states anchoring disconnected components.
  /// @return Success or the first construction failure.
  SsaResult<void> run(InstructionEffects& simulator, std::span<const StackMapFrame> seeds);

private:
  /// Account for bounded symbolic execution and poll cancellation.
  /// @param amount Number of construction work units.
  /// @return Success or a cancellation or limit diagnostic.
  SsaResult<void> spend(std::size_t amount = 1) {
    if (storage.limits.cancellation.stop_requested())
      return std::unexpected(SsaError{.kind = SsaErrorKind::Cancelled, .detail = "SSA construction cancelled"});
    if (amount > remaining)
      return std::unexpected(SsaError{.kind = SsaErrorKind::ResourceLimitExceeded, .detail = "SSA construction work limit exceeded"});
    remaining -= amount;
    return {};
  }

  /// Bind verifier allocation identities to SSA new operations.
  /// @param type Source verifier type.
  /// @return An SSA type with the corresponding allocation identity.
  SsaType type_of(VerificationType type) const {
    SsaType result{type.kind, type.reference};
    if (type.kind == VerificationKind::Uninitialized) {
      const auto it = allocation_sites.find(type.allocation.index);
      if (it != allocation_sites.end())
        result.allocation = it->second;
    }
    return result;
  }

  /// Build ordered blocks and operations, recording source instruction identities.
  /// @return Success or a graph allocation or edit failure.
  SsaResult<void> layout();
  /// Install branch, exception, and root transfers.
  /// @param seeds Declared frames for non-executable roots.
  /// @return Success or a graph construction failure.
  SsaResult<void> connect(std::span<const StackMapFrame> seeds);
  /// Identify source components for otherwise disconnected code.
  /// @param seeds Declared verifier frames in source order.
  /// @return Success or a missing-state or budget error.
  SsaResult<void> add_roots(std::span<const StackMapFrame> seeds);
  /// Add verifier-only input definitions to a disconnected root.
  /// @param block Component entry with a verified frame.
  /// @return Success or a graph construction failure.
  SsaResult<void> seed_block(SsaBlockId block);
  /// Symbolically replay one block from its typed input frame.
  /// @param b Source block index.
  /// @param simulator Prepared, shared JVM instruction transfer engine.
  /// @return Success or a transfer or graph failure.
  SsaResult<void> simulate(std::uint32_t b, InstructionEffects& simulator);
  /// Resolve demanded incoming definitions along every predecessor edge.
  /// @return Success or a missing or incompatible incoming value error.
  SsaResult<void> seal();
  /// Propagate trivial phis and retire definitions unreachable from uses.
  /// @return Success or a work or memory limit error.
  SsaResult<void> simplify();
  /// Demand a concrete SSA value for a symbolic reference.
  /// @param symbol Existing definition or block entry frame cell.
  /// @return The definition, allocating an incomplete phi on first demand.
  SsaResult<SsaValueId> materialize(Symbol symbol);
  /// Find the local value visible just before an instruction boundary.
  /// @param b Block containing the boundary.
  /// @param slot Zero-based JVM local slot.
  /// @param cutoff Instruction position of the boundary, exclusive for writes.
  /// @return The latest written symbol or deferred block entry.
  Symbol local_at(std::uint32_t b, std::uint32_t slot, std::uint32_t cutoff) const;
  /// Read a frame cell at the exact program point of a transfer.
  /// @param edge Incoming transfer, including exceptional and verifier-only transfers.
  /// @param cell Target local slot or biased stack ordinal.
  /// @return The source symbol or a shape mismatch.
  SsaResult<Symbol> incoming(const SsaEdge& edge, std::uint32_t cell) const;
  /// Bind an SSA result to its defining operation.
  /// @param op Defining operation.
  /// @param type Verified normal-completion result type.
  /// @return The fresh scoped definition.
  SsaValueId result(SsaOperationId op, SsaType type) {
    auto& operation = storage.operations[op.index];
    auto value = storage.add_value(SsaValueKind::Operation, operation.block, type);
    storage.values[value.index].operation = op;
    operation.result = value;
    return value;
  }

  const ClassFile& owner; ///< Class held by the lift call.
  const MethodCode& code; ///< Original logical instructions.
  SsaMethod& graph; ///< Destination SSA method.
  SsaStorage& storage; ///< Private storage of the destination graph.
  const ControlFlowGraph& cfg; ///< Verified source blocks and ordered handler ranges.
  const SsaFrames& frames; ///< Converged verifier entries in CFG order.
  std::pmr::memory_resource* resource; ///< Destination graph budget shared by scratch allocations.
  std::size_t remaining; ///< Remaining symbolic construction work.
  bool keep_locals = false; ///< Whether local assignments are observable by source metadata.
  std::pmr::vector<SsaOperationId> origin; ///< Operation at each source instruction position.
  std::pmr::vector<std::uint32_t> position; ///< Source position of each original operation.
  std::pmr::unordered_map<std::uint32_t, SsaOperationId> allocation_sites; ///< SSA allocation by source instruction slot.
  std::pmr::unordered_map<std::uint64_t, SsaValueId> roots; ///< Method and disconnected root input definitions.
  std::pmr::unordered_map<std::uint64_t, std::pmr::vector<LocalWrite>> changes; ///< Per-block sparse local assignment histories.
  std::pmr::vector<std::pmr::vector<Symbol>> exits; ///< Exit identities indexed by JVM stack slot.
  std::pmr::vector<std::pmr::vector<unsigned>> entry_offsets; ///< JVM slot of each entry stack value.
  std::pmr::unordered_map<std::uint64_t, SsaValueId> phis; ///< Provisional definition for each demanded frame cell.
  std::pmr::vector<std::uint64_t> pending; ///< Demanded frame cells in discovery order.
};

SsaResult<void> Lifter::layout() {
  if (auto work = spend(code.size()); !work)
    return work;
  if (frames.entries.size() != cfg.blocks().size())
    return std::unexpected(SsaError{.detail = "an SSA block has no verified incoming frame"});
  for (std::size_t b = 1; b < cfg.blocks().size(); ++b)
    if (auto id = graph.add_block(); !id)
      return std::unexpected(std::move(id.error()));
  exits.resize(cfg.blocks().size());
  entry_offsets.resize(cfg.blocks().size());
  origin.resize(code.size());
  for (const auto& block : cfg.blocks()) {
    const auto index = static_cast<std::size_t>(&block - cfg.blocks().data());
    const auto id = storage.blocks[index].id;
    for (auto p = block.begin; p < block.end; ++p) {
      const auto at = code.at(p);
      const auto insn = *code.get(at);
      const auto opcode = insn.opcode();
      const bool anchor = is_load(opcode) || is_store(opcode) || (opcode >= Opcode::Pop && opcode <= Opcode::Swap);
      const auto flow = opcode_info(opcode).control_flow;
      auto added = flow == ControlFlow::Next ? graph.append_operation(id, anchor ? Instruction::nop() : insn)
                                             : graph.append_terminator(id, opcode);
      if (!added)
        return std::unexpected(std::move(added.error()));
      origin[p] = *added;
      position.resize(added->index + 1);
      position[added->index] = p;
      auto& operation = storage.operations[added->index];
      operation.origin = at;
      operation.instruction = insn;
      operation.anchor = anchor;
      if (opcode == Opcode::New)
        allocation_sites.emplace(at.index, *added);
    }
    if (opcode_info(code.get(block.last)->opcode()).control_flow == ControlFlow::Next) {
      if (auto added = graph.append_terminator(id, Opcode::Goto); !added)
        return std::unexpected(std::move(added.error()));
    }
  }
  return {};
}

SsaResult<void> Lifter::seed_block(SsaBlockId block) {
  const auto& frame = frames.entries[block.index].frame;
  auto edge = graph.add_dead_root(block, frame.constructor_uninitialized);
  if (!edge)
    return std::unexpected(std::move(edge.error()));
  const auto bind = [&](std::span<const VerificationType> types, SsaValueKind kind, std::uint32_t base) -> SsaResult<void> {
    if (auto work = spend(types.size()); !work)
      return work;
    for (std::uint32_t i = 0; i < types.size(); ++i) {
      if (!usable(types[i]))
        continue;
      auto value = graph.add_root_input(*edge, kind, i, type_of(types[i]));
      if (!value)
        return std::unexpected(std::move(value.error()));
      roots.emplace(port(edge->index, base + i), *value);
    }
    return {};
  };
  if (auto locals = bind(frame.locals, SsaValueKind::RootLocal, 0); !locals)
    return locals;
  return bind(frame.stack, SsaValueKind::RootStack, STACK_BASE);
}

SsaResult<void> Lifter::add_roots(std::span<const StackMapFrame> seeds) {
  for (const auto value : graph.parameters())
    roots.emplace(port(graph.entry_edge().index, storage.values[value.index].slot), value);
  if (std::ranges::all_of(cfg.blocks(), &BasicBlock::reachable))
    return {};

  // A source SCC needs just one non-executable root. First DFS from declared frame sites, then
  // traverse in reverse finishing order so a downstream declaration never creates a spurious root.
  std::pmr::vector<bool> seen(resource);
  for (const auto& block : cfg.blocks())
    seen.push_back(block.reachable);
  const auto initially_seen = std::pmr::vector<bool>(seen, resource);
  struct Cursor {
    std::uint32_t block;
    std::size_t next;
  };
  std::pmr::vector<Cursor> path(resource);
  std::pmr::vector<std::uint32_t> order(resource);
  const auto visit = [&](std::uint32_t start, bool record) -> SsaResult<void> {
    seen[start] = true;
    path.push_back({start, 0});
    while (!path.empty()) {
      if (auto work = spend(); !work)
        return work;
      auto& cursor = path.back();
      const auto& block = storage.blocks[cursor.block];
      if (cursor.next == block.outgoing.size()) {
        if (record)
          order.push_back(cursor.block);
        path.pop_back();
      } else {
        const auto target = storage.edges[block.outgoing[cursor.next++].index].target.index;
        if (!seen[target]) {
          seen[target] = true;
          path.push_back({target, 0});
        }
      }
    }
    return {};
  };
  for (const auto& seed : seeds) {
    const auto block = cfg.block_of(seed.instruction);
    if (block && !seen[block->value])
      if (auto visited = visit(block->value, true); !visited)
        return visited;
  }
  if (std::ranges::find(seen, false) != seen.end())
    return std::unexpected(from_analysis({.kind = AnalysisErrorKind::UnreachableCode, .detail = "disconnected code has no declared frame root"}));
  seen = initially_seen;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (seen[*it])
      continue;
    if (auto seeded = seed_block(storage.blocks[*it].id); !seeded)
      return seeded;
    if (auto visited = visit(*it, false); !visited)
      return visited;
  }
  return {};
}

SsaResult<void> Lifter::connect(std::span<const StackMapFrame> seeds) {
  std::pmr::vector<SsaType> catch_types(resource);
  for (const auto& range : cfg.exception_ranges()) {
    auto row = graph.add_handler(storage.blocks[range.target.value].id, range.handler.catch_type);
    if (!row)
      return std::unexpected(std::move(row.error()));
    storage.handlers[*row].origin = static_cast<std::uint16_t>(*row);
    const auto name = range.handler.catch_type ? detail::class_name(owner.constant_pool(), range.handler.catch_type)
                                               : std::optional<std::string_view>{"java/lang/Throwable"};
    if (!name)
      return std::unexpected(SsaError{.detail = "invalid catch class"});
    catch_types.push_back({VerificationKind::Object, *name});
  }
  std::pmr::vector<std::uint32_t> handlers(resource);
  for (std::uint32_t b = 0; b < cfg.blocks().size(); ++b) {
    for (const auto& edge : cfg.outgoing(BlockId{b})) {
      if (auto added = graph.add_edge(storage.blocks[b].id, storage.blocks[edge.target.value].id, edge_kind(edge.kind), edge.case_key); !added)
        return std::unexpected(std::move(added.error()));
    }
    cfg.handlers_for(BlockId{b}, handlers);
    if (handlers.empty())
      continue;
    const auto& block = cfg.blocks()[b];
    for (auto p = block.begin; p < block.end; ++p) {
      if (auto work = spend(handlers.size()); !work)
        return work;
      for (const auto row : handlers)
        if (auto edge = graph.add_exception_edge(origin[p], row, catch_types[row]); !edge)
          return std::unexpected(std::move(edge.error()));
    }
  }
  return add_roots(seeds);
}

Symbol Lifter::local_at(std::uint32_t b, std::uint32_t slot, std::uint32_t cutoff) const {
  const auto key = port(b, slot);
  if (const auto it = changes.find(key); it != changes.end()) {
    const auto& writes = it->second;
    const auto end = std::ranges::lower_bound(writes, cutoff, {}, &LocalWrite::position);
    if (end != writes.begin())
      return std::prev(end)->symbol;
  }
  return {.input = key};
}

SsaResult<SsaValueId> Lifter::materialize(Symbol symbol) {
  if (symbol.value)
    return symbol.value;
  if (symbol.input == NO_PORT)
    return std::unexpected(SsaError{.detail = "unusable verifier slot used as an SSA value"});
  if (const auto it = phis.find(symbol.input); it != phis.end())
    return it->second;
  const auto b = static_cast<std::uint32_t>(symbol.input >> 32);
  const auto cell = static_cast<std::uint32_t>(symbol.input);
  const auto& frame = frames.entries[b].frame;
  const auto type = cell < STACK_BASE ? frame.locals[cell] : frame.stack[cell - STACK_BASE];
  if (!usable(type))
    return std::unexpected(SsaError{.detail = "unusable verifier slot used as a phi input", .block = storage.blocks[b].id});
  auto phi = graph.add_phi(storage.blocks[b].id, type_of(type));
  if (!phi)
    return std::unexpected(std::move(phi.error()));
  phis.emplace(symbol.input, *phi);
  pending.push_back(symbol.input);
  return *phi;
}

SsaResult<Symbol> Lifter::incoming(const SsaEdge& edge, std::uint32_t cell) const {
  if (edge.kind == SsaEdgeKind::Entry || edge.kind == SsaEdgeKind::DeadRoot) {
    const auto root = roots.find(port(edge.id.index, cell));
    if (root == roots.end())
      return std::unexpected(SsaError{.detail = "root has no incoming value for this frame cell", .edge = edge.id});
    return Symbol{.value = root->second};
  }
  const bool exceptional = edge.kind == SsaEdgeKind::Exceptional;
  if (cell < STACK_BASE) {
    const auto cutoff = exceptional ? position[edge.site.index] : cfg.blocks()[edge.source.index].end;
    return local_at(edge.source.index, cell, cutoff);
  }
  if (exceptional) {
    if (cell != STACK_BASE)
      return std::unexpected(SsaError{.detail = "exception transfer has one stack value", .edge = edge.id});
    return Symbol{.value = edge.exception};
  }
  const auto slot = entry_offsets[edge.target.index][cell - STACK_BASE];
  const auto& stack = exits[edge.source.index];
  if (slot >= stack.size())
    return std::unexpected(SsaError{.detail = "incompatible incoming stack", .edge = edge.id});
  return stack[slot];
}

SsaResult<void> Lifter::simulate(std::uint32_t b, InstructionEffects& simulator) {
  const auto& block = cfg.blocks()[b];
  const auto& entry = frames.entries[b].frame;
  Frame state(resource);
  Frame before_call(resource);
  if (auto work = spend(entry.locals.size() + entry.stack.size()); !work)
    return work;
  copy_frame(state, entry);
  std::pmr::vector<Symbol> stack(resource);
  unsigned offset = 0;
  entry_offsets[b].reserve(entry.stack.size());
  stack.reserve(entry.stack.size());
  for (std::uint32_t i = 0; i < entry.stack.size(); ++i) {
    entry_offsets[b].push_back(offset);
    offset += entry.stack[i].width();
    stack.push_back(usable(entry.stack[i]) ? Symbol{.input = port(b, STACK_BASE + i)} : Symbol{});
  }

  for (auto p = block.begin; p < block.end; ++p) {
    const auto id = origin[p];
    auto& op = storage.operations[id.index];
    const auto opcode = op.instruction.opcode();
    if (auto work = spend(); !work)
      return work;
    if (opcode == Opcode::InvokeSpecial) {
      if (auto work = spend(state.locals.size() + state.stack.size()); !work)
        return work;
      copy_frame(before_call, state);
    }
    InstructionTransfer transfer;
    auto initialized = simulator.apply(state, code.at(p), op.instruction, &transfer);
    if (!initialized)
      return std::unexpected(from_analysis(std::move(initialized.error())));
    if (transfer.popped > stack.size())
      return std::unexpected(SsaError{.detail = "inconsistent symbolic operand stack", .operation = id});
    const auto base = stack.size() - transfer.popped;
    if (transfer.reordered) {
      std::array<Symbol, 6> suffix;
      for (std::size_t i = 0; i < transfer.stack_map_size; ++i)
        suffix[i] = stack[base + transfer.stack_map[i]];
      stack.resize(base);
      stack.insert(stack.end(), suffix.begin(), suffix.begin() + static_cast<std::ptrdiff_t>(transfer.stack_map_size));
      continue;
    }
    if (is_load(opcode)) {
      stack.push_back(local_at(b, *op.instruction.local_index(), p));
      continue;
    }
    if (is_store(opcode)) {
      const auto slot = *op.instruction.local_index();
      const auto stored = stack.back();
      changes[port(b, slot)].push_back({p, stored});
      stack.pop_back();
      if (keep_locals) {
        auto value = materialize(stored);
        if (!value)
          return std::unexpected(std::move(value.error()));
        op.local_assignment = SsaLocalAssignment{slot, *value};
      }
      continue;
    }
    if (opcode == Opcode::IInc) {
      const auto slot = op.instruction.increment_value()->local;
      auto old = materialize(local_at(b, slot, p));
      if (!old)
        return std::unexpected(std::move(old.error()));
      op.inputs.push_back(*old);
      const auto updated = result(id, {VerificationKind::Integer});
      changes[port(b, slot)].push_back({p, {.value = updated}});
      if (keep_locals)
        op.local_assignment = SsaLocalAssignment{slot, updated};
      continue;
    }
    if (auto work = spend(transfer.popped); !work)
      return work;
    op.inputs.reserve(transfer.popped);
    for (std::size_t i = base; i < stack.size(); ++i) {
      auto operand = materialize(stack[i]);
      if (!operand)
        return std::unexpected(std::move(operand.error()));
      op.inputs.push_back(*operand);
    }
    stack.resize(base);
    if (*initialized) {
      const auto receiver = **initialized;
      const auto name = receiver.kind == VerificationKind::UninitializedThis
                            ? detail::class_name(owner.constant_pool(), owner.this_class()).value_or(std::string_view{})
                            : receiver.reference;
      const auto value = result(id, {VerificationKind::Object, name});
      for (std::uint32_t i = 0; i < before_call.locals.size(); ++i)
        if (before_call.locals[i] == receiver)
          changes[port(b, i)].push_back({p, {.value = value}});
      for (std::size_t i = 0; i < stack.size(); ++i)
        if (before_call.stack[i] == receiver)
          stack[i] = {.value = value};
    } else if (transfer.produced) {
      stack.push_back({.value = result(id, type_of(state.stack.back()))});
    }
    const auto flow = opcode_info(opcode).control_flow;
    if (flow == ControlFlow::Return || flow == ControlFlow::Throw)
      stack.clear();
  }
  // An explicit frame may forget one wide stack value as two independent Top slots.
  // Match outgoing and incoming cells by JVM word offset even when their ordinals differ.
  if (auto work = spend(state.stack_slots); !work)
    return work;
  exits[b].reserve(state.stack_slots);
  for (std::size_t i = 0; i < stack.size(); ++i) {
    exits[b].push_back(stack[i]);
    if (state.stack[i].width() == 2)
      exits[b].push_back({});
  }
  return {};
}

SsaResult<void> Lifter::seal() {
  // Materializing an input can discover another phi, including one in a successor on a backedge.
  for (std::size_t i = 0; i < pending.size(); ++i) {
    const auto key = pending[i];
    const auto cell = static_cast<std::uint32_t>(key);
    const auto id = phis.find(key)->second;
    const auto& block = storage.blocks[storage.values[id.index].block.index];
    if (auto work = spend(block.incoming.size()); !work)
      return work;
    storage.values[id.index].incoming.reserve(block.incoming.size());
    for (const auto edge : block.incoming) {
      auto symbol = incoming(storage.edges[edge.index], cell);
      if (!symbol)
        return std::unexpected(std::move(symbol.error()));
      auto value = materialize(*symbol);
      if (!value)
        return std::unexpected(std::move(value.error()));
      // The graph is private until every incoming edge has exactly one argument.
      storage.values[id.index].incoming.push_back({edge, *value});
    }
  }
  return {};
}

SsaResult<void> Lifter::simplify() {
  std::pmr::vector<SsaValueId> aliases(resource), queue(resource);
  std::pmr::vector<std::pmr::vector<SsaValueId>> dependents(resource);
  std::pmr::vector<bool> queued(storage.values.size(), false, resource);
  dependents.resize(storage.values.size());
  aliases.reserve(storage.values.size());
  for (const auto& value : storage.values) {
    aliases.push_back(value.id);
    if (value.kind == SsaValueKind::Phi) {
      if (auto work = spend(value.incoming.size()); !work)
        return work;
      queue.push_back(value.id);
      queued[value.id.index] = true;
      for (const auto& input : value.incoming)
        dependents[input.value.index].push_back(value.id);
    }
  }
  const auto resolve = [&](SsaValueId value) {
    while (value != aliases[value.index]) {
      const auto next = aliases[value.index];
      aliases[value.index] = aliases[next.index];
      value = aliases[value.index];
    }
    return value;
  };
  while (!queue.empty()) {
    const auto id = queue.back();
    queue.pop_back();
    queued[id.index] = false;
    if (aliases[id.index] != id)
      continue;
    const auto& phi = storage.values[id.index];
    if (auto work = spend(1 + phi.incoming.size()); !work)
      return work;
    SsaValueId other{};
    for (const auto& input : phi.incoming) {
      const auto candidate = resolve(input.value);
      if (candidate == id)
        continue;
      if (other && other != candidate) {
        other = {};
        break;
      }
      other = candidate;
    }
    if (!other)
      continue;
    const auto& replacement = storage.values[other.index];
    if (replacement.kind == SsaValueKind::Exception || replacement.kind == SsaValueKind::RootLocal ||
        replacement.kind == SsaValueKind::RootStack || replacement.type != phi.type)
      continue;
    aliases[id.index] = other;
    auto& uses = dependents[id.index];
    if (auto work = spend(uses.size()); !work)
      return work;
    for (const auto use : uses) {
      if (aliases[use.index] != use)
        continue;
      if (!queued[use.index]) {
        queued[use.index] = true;
        queue.push_back(use);
      }
      if (replacement.kind == SsaValueKind::Phi)
        dependents[other.index].push_back(use);
    }
    uses.clear();
  }

  std::pmr::vector<bool> live(storage.values.size(), false, resource);
  const auto mark = [&](SsaValueId& value) {
    value = resolve(value);
    if (storage.values[value.index].kind == SsaValueKind::Phi && !live[value.index]) {
      live[value.index] = true;
      queue.push_back(value);
    }
  };
  for (auto& op : storage.operations) {
    if (auto work = spend(1 + op.inputs.size()); !work)
      return work;
    for (auto& input : op.inputs)
      mark(input);
    auto assignment = op.local_assignment.value_or(SsaLocalAssignment{});
    if (assignment.value) {
      mark(assignment.value);
      op.local_assignment = assignment;
    }
  }
  while (!queue.empty()) {
    const auto phi = queue.back();
    queue.pop_back();
    auto& inputs = storage.values[phi.index].incoming;
    if (auto work = spend(inputs.size()); !work)
      return work;
    for (auto& input : inputs)
      mark(input.value);
  }
  for (auto& block : storage.blocks) {
    std::erase_if(block.phis, [&](SsaValueId id) {
      if (live[id.index])
        return false;
      auto& value = storage.values[id.index];
      value.id = {};
      std::pmr::vector<SsaPhiInput>(resource).swap(value.incoming);
      return true;
    });
  }
  return {};
}

SsaResult<void> Lifter::run(InstructionEffects& simulator, std::span<const StackMapFrame> seeds) {
  if (auto built = layout(); !built)
    return built;
  if (auto connected = connect(seeds); !connected)
    return connected;
  for (std::uint32_t b = 0; b < cfg.blocks().size(); ++b)
    if (auto simulated = simulate(b, simulator); !simulated)
      return simulated;
  if (auto sealed = seal(); !sealed)
    return sealed;
  return simplify();
}

} // namespace

SsaResult<SsaMethod> lift_ssa(const ClassFile& owner, std::size_t method_index, AnalysisContext& context) try {
  auto graph = create_ssa(owner, method_index, context);
  if (!graph)
    return graph;
  const auto& method = owner.methods()[method_index];
  if (!method.code())
    return std::unexpected(from_analysis({.kind = AnalysisErrorKind::MissingCode, .detail = "SSA lifting requires a method body"}));
  auto limits = context.limits();
  limits.upstream_resource = &SsaAccess::get(*graph).budget;
  AnalysisContext bounded(context.hierarchy(), limits);
  const auto& body = *method.code();
  auto declared = decode_ssa_frames(owner, method, limits);
  if (!declared)
    return std::unexpected(from_analysis(std::move(declared.error())));
  auto frames = ssa_frames(owner, method, body.code(), body.exception_handlers(), bounded, declared->entries);
  if (!frames)
    return std::unexpected(from_analysis(std::move(frames.error())));
  auto cfg = ssa_cfg(body.code(), body.exception_handlers(), declared->entries, limits.upstream_resource, limits);
  if (!cfg)
    return std::unexpected(from_analysis(std::move(cfg.error())));
  auto hierarchy = context.hierarchy().with_class(owner);
  if (!hierarchy)
    return std::unexpected(from_analysis({.kind = AnalysisErrorKind::TypeResolutionFailure, .detail = hierarchy.error().detail, .resolution = std::move(hierarchy.error())}));
  InstructionEffects simulator(owner, method, &*hierarchy, limits.upstream_resource, nullptr, true);
  Frame initial(limits.upstream_resource);
  if (auto ok = simulator.initialize(initial, frames->max_locals); !ok)
    return std::unexpected(from_analysis(std::move(ok.error())));
  for (const auto& item : body.code().instructions()) {
    if (limits.cancellation.stop_requested())
      return std::unexpected(SsaError{.kind = SsaErrorKind::Cancelled, .detail = "SSA construction cancelled"});
    if (auto ok = simulator.prepare(item.id, item.instruction, frames->max_locals); !ok)
      return std::unexpected(from_analysis(std::move(ok.error())));
  }
  Lifter lifter(owner, body, *graph, *cfg, *frames);
  if (auto ok = lifter.run(simulator, declared->entries); !ok)
    return std::unexpected(std::move(ok.error()));
  return graph;
} catch (const detail::BudgetExceeded&) {
  return std::unexpected(SsaError{.kind = SsaErrorKind::ResourceLimitExceeded, .detail = "SSA construction memory limit exceeded"});
}

} // namespace cafe::analysis
