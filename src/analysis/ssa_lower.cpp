/**
 * @file
 * @brief Destroy SSA into a staged, verifier-checked JVM method body.
 * @ingroup implementation
 */
#include "ssa_slots.hpp"

#include <cafe/bytecode/code.hpp>
#include <cafe/class_file.hpp>

#include "bytecode/code_access.hpp"
#include "io/ssa_relocation.hpp"

#include <algorithm>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace cafe::analysis {
namespace {

/// Verifier category carried by a typed copy or load/store.
using Kind = VerificationKind;

/**
 * @brief Classify verification types by their JVM copy instruction.
 * @param kind Verifier category of the value.
 * @return Primitive category or Object for every reference-like category.
 */
Kind move_kind(Kind kind) {
  switch (kind) {
    case Kind::Integer:
    case Kind::Long:
    case Kind::Float:
    case Kind::Double:
      return kind;
    default:
      return Kind::Object;
  }
}

/**
 * @brief Select a logical local load or store.
 * @param kind Verification category of the value.
 * @param slot First physical JVM local slot.
 * @param store Whether to store the top operand into the local.
 * @return A typed local instruction.
 */
Instruction access(Kind kind, std::uint16_t slot, bool store) {
  switch (kind) {
    case Kind::Integer:
      return store ? Instruction::istore(slot) : Instruction::iload(slot);
    case Kind::Long:
      return store ? Instruction::lstore(slot) : Instruction::lload(slot);
    case Kind::Float:
      return store ? Instruction::fstore(slot) : Instruction::fload(slot);
    case Kind::Double:
      return store ? Instruction::dstore(slot) : Instruction::dload(slot);
    default:
      return store ? Instruction::astore(slot) : Instruction::aload(slot);
  }
}

/// Single member of a simultaneous edge-copy bundle.
struct Move {
  std::uint32_t from; ///< Source local, or SSA_NO_SLOT for the caught exception.
  std::uint32_t to; ///< Destination local.
  Kind kind; ///< JVM copy category.
  /**
   * @brief Compare two physical copy descriptions.
   * @param right Second move.
   * @return Whether source, destination and category match.
   */
  bool operator==(const Move& right) const = default;
};

/**
 * @brief Count the occupied local slots for one move.
 * @param kind Category being transported.
 * @return One or two JVM slots.
 */
unsigned width(Kind kind) {
  return kind == Kind::Double || kind == Kind::Long ? 2 : 1;
}

/**
 * @brief Test two half-open physical slot intervals.
 * @param a First interval start.
 * @param a_width First interval width.
 * @param b Second interval start.
 * @param b_width Second interval width.
 * @return Whether a write could clobber any source slot.
 */
bool overlap(std::uint32_t a, unsigned a_width, std::uint32_t b, unsigned b_width) {
  return a < b + b_width && b < a + a_width;
}

/**
 * @brief Translate staged writer failures to public SSA diagnostics.
 * @param error Writer failure with a structural method path.
 * @return An owned SSA diagnostic.
 */
SsaError write_failure(const WriteError& error) {
  SsaErrorKind kind = SsaErrorKind::EmissionFailure;
  switch (error.kind) {
    case WriteErrorKind::ResourceLimitExceeded:
      kind = SsaErrorKind::ResourceLimitExceeded;
      break;
    case WriteErrorKind::Cancelled:
      kind = SsaErrorKind::Cancelled;
      break;
    case WriteErrorKind::StaleAnalysisResult:
    case WriteErrorKind::StaleCodeMetadata:
      kind = SsaErrorKind::StaleSource;
      break;
    case WriteErrorKind::UnmappedCodeOffset:
    case WriteErrorKind::UnsafeOpaqueAttribute:
      kind = SsaErrorKind::UnsafeMetadata;
      break;
    default:
      break;
  }
  return {.kind = kind, .detail = error.path + ": " + error.detail, .write_error_kind = error.kind};
}

/// A contiguous emitted interval targeting a common exception pad.
struct Segment {
  std::size_t begin; ///< Inclusive staged instruction position.
  std::size_t end; ///< Exclusive staged instruction position.
  LabelId target; ///< Landing pad for this protected interval.
};

/// Physical stack/local scheduling, exception-range reconstruction and transactional emission.
class Emitter {
public:
  /**
   * @brief Prepare temporary state for emission from a verified graph.
   * @param graph Verified SSA graph.
   * @param source Existing method body or null for a new method.
   * @param debug_homes Dedicated homes of source slots referenced by metadata.
   * @param debug_count Physical slots reserved for parameters and debug homes.
   * @param metadata_anchors Original instructions anchoring metadata.
   * @param slots Physical home assignments and available scratch slots.
   * @param commit Transaction owning the staged bytecode and resolved constants.
   * @param budget Shared lowering work and cancellation state.
   * @param memory Bounded allocator covering scratch and logical staging.
   */
  Emitter(const SsaStorage& graph, const CodeAttribute* source, std::span<const std::uint32_t> debug_homes,
          std::size_t debug_count, std::span<const std::uint8_t> metadata_anchors, SsaSlots& slots,
          detail::SsaCommit& commit, SsaLowerBudget& budget, detail::BudgetResource& memory) :
      graph(graph), source(source), debug_homes(debug_homes), metadata_anchors(metadata_anchors), slots(slots), commit(commit), code(commit.code()),
      budget(budget), memory(memory), accounted_bytes(commit.class_allocation_bytes()), scratch(&memory),
      bodies(scratch), block_labels(graph.blocks.size(), scratch), targets(graph.edges.size(), scratch),
      moves(graph.edges.size(), scratch), predecessor_moves(graph.blocks.size(), scratch), handler_entries(graph.blocks.size(), scratch),
      exceptional(graph.operations.size(), scratch), root_edges(graph.blocks.size(), scratch),
      root_values(graph.edges.size(), scratch), root_positions(graph.edges.size(), scratch), pad_edges(scratch),
      bindings(scratch), boundaries(scratch), exact(graph.operations.size(), scratch), ranges(graph.handlers.size(), scratch),
      handlers(scratch), seeds(scratch), provenance(scratch) {
    provenance.debug_local_count = debug_count;
  }

  /**
   * @brief Emit all bodies and pads, verify and install the result.
   * @param context Verifier context used to validate the final layout.
   * @param options Requested unsafe metadata handling.
   * @return Committed recoveries or a failure leaving the original body intact.
   */
  SsaResult<SsaLowerResult> run(AnalysisContext& context, const SsaLowerOptions& options);

private:
  const SsaStorage& graph; ///< Verified graph supplying instructions and edges.
  const CodeAttribute* source; ///< Original Code, if present.
  std::span<const std::uint32_t> debug_homes; ///< Physical home for each metadata-visible source slot.
  std::span<const std::uint8_t> metadata_anchors; ///< Source instruction indices referenced by offset-bearing attributes.
  /// Check whether metadata names a source local.
  /// @param slot Source local index.
  /// @return Whether the local has a reserved output home.
  bool has_debug_home(std::size_t slot) const {
    return slot < debug_homes.size() && debug_homes[slot] != SSA_NO_SLOT;
  }
  SsaSlots& slots; ///< Allocated homes and stack scheduling flags.
  detail::SsaCommit& commit; ///< Staged class and bytecode transaction.
  MethodCode& code; ///< Detached bytecode owned by commit.
  SsaLowerBudget& budget; ///< Work and cancellation state.
  detail::BudgetResource& memory; ///< Shared ceiling for PMR scratch and unpublished class allocations.
  std::size_t accounted_bytes; ///< High-water mark of allocations on the owning class resource.
  std::pmr::memory_resource* scratch; ///< Storage for the lowering operation.
  std::pmr::vector<SsaBlockId> bodies; ///< Blocks that emit a body, in physical order.
  std::pmr::vector<LabelId> block_labels; ///< Body label for each block, after verifier-root entry copies.
  std::pmr::vector<LabelId> targets; ///< Direct body or edge-pad label for each normal or exceptional edge.
  std::pmr::vector<std::pmr::vector<Move>> moves; ///< Simultaneous assignments for each incoming transfer.
  std::pmr::vector<SsaEdgeId> predecessor_moves; ///< Representative normal bundle emitted before a block's terminator.
  std::pmr::vector<SsaEdgeId> handler_entries; ///< Shared exceptional bundle emitted at an exclusively exceptional entry.
  std::pmr::vector<std::pmr::vector<SsaEdgeId>> exceptional; ///< Exceptional edge list at each protected operation.
  std::pmr::vector<std::pmr::vector<SsaEdgeId>> root_edges; ///< Synthetic entry transfers at each block.
  std::pmr::vector<std::pmr::vector<SsaValueId>> root_values; ///< Verifier-only local and stack inputs for each dead root.
  std::pmr::vector<std::size_t> root_positions; ///< Instruction positions where lowered root frames must be declared.
  std::pmr::vector<SsaEdgeId> pad_edges; ///< Representative edges for distinct out-of-line pads.
  std::pmr::vector<std::pair<LabelId, std::size_t>> bindings; ///< Label placements deferred until the entire instruction order is known.
  std::pmr::unordered_map<std::size_t, LabelId> boundaries; ///< Deduplicated labels at positions used by metadata.
  std::pmr::vector<InstructionId> exact; ///< Staged instruction implementing each original operation.
  std::pmr::vector<std::pmr::vector<Segment>> ranges; ///< Coalesced protected intervals in original clause order.
  std::pmr::vector<ExceptionHandler> handlers; ///< Reconstructed ordered exception table.
  std::pmr::vector<StackMapFrame> seeds; ///< Verifier states for disconnected regions.
  detail::SsaCodeProvenance provenance; ///< Original instructions, variables and handler rows for offset relocation.
  std::optional<SsaError> failure; ///< First failure in staged emission.
  SsaOperationId current{}; ///< Current operation for diagnostic attribution.
  std::uint32_t caught_slot = SSA_NO_SLOT; ///< Scratch home for a pad consuming a caught exception.

  /**
   * @brief Check cancellation and charge work.
   * @param amount Work units needed for the next operation.
   * @return Whether emission may continue.
   */
  bool charge(std::size_t amount);
  /// Charge storage staged onto the class-owned resource, which must outlive lowering.
  void account_staged_storage();
  /**
   * @brief Record the first failure at the current operation.
   * @param kind Structured failure category.
   * @param reason Diagnostic text to retain.
   */
  void fail(SsaErrorKind kind, std::string reason);
  /**
   * @brief Allocate an unbound staged control-flow or metadata label.
   * @return The new label or an invalid handle after an error.
   */
  LabelId label();
  /**
   * @brief Intern a staged position as a metadata label.
   * @param position Instruction-order boundary, including method end.
   * @return The label identifying this boundary.
   */
  LabelId boundary(std::size_t position);
  /**
   * @brief Append one logical JVM instruction to the staged body.
   * @param instruction Validated local, branch, switch, or exact instruction.
   * @return Its staged handle or an invalid handle after an error.
   */
  InstructionId emit(const InstructionBuilder& instruction);
  /**
   * @brief Append an operand-free logical instruction.
   * @param op Requested simple JVM opcode.
   * @return Its staged handle or an invalid handle after failure.
   */
  InstructionId emit_simple(Opcode op);
  /**
   * @brief Materialize an SSA operand at the top of the JVM expression stack.
   * @param value Definition required next in evaluation order.
   */
  void reference(SsaValueId value);
  /// @brief Store or discard a stack result that will not be carried to its user.
  /// @param value SSA definition currently on the JVM stack.
  void store_result(SsaValueId value);
  /// @brief Increment a coalesced local in place or produce the sum on the stack.
  /// @param op Verified integer increment.
  /// @param core Receives the staged position to protect for exceptional transfers.
  void increment(const SsaOperation& op, std::size_t& core);
  /**
   * @brief Record a physical copy required by a phi or handler input.
   * @param edge Transfer on which both homes are valid.
   * @param from Source local or SSA_NO_SLOT for the caught exception.
   * @param destination SSA definition receiving the value.
   */
  void add_move(SsaEdgeId edge, std::uint32_t from, SsaValueId destination);
  /// @brief Check whether a source instruction anchors offset-bearing metadata.
  /// @param op Candidate source operation.
  /// @return Whether its original instruction boundary must remain in the emitted code.
  bool metadata_anchor(const SsaOperation& op) const;
  /// Thread unprotected, unanchored jump-only chains and choose placement for normal copies.
  void layout_blocks();
  /**
   * @brief Create block labels, parallel-copy bundles and shared pad destinations.
   */
  void prepare();
  /**
   * @brief Emit simultaneous copies without overwriting unread values.
   * @param bundle Typed home-to-home assignments.
   * @param from_handler Whether the JVM supplied a caught exception on entry.
   */
  void copies(std::span<const Move> bundle, bool from_handler);
  /**
   * @brief Emit moves for a method entry or disconnected verifier root.
   * @param edge Synthetic incoming edge.
   */
  void root(const SsaEdge& edge);
  /**
   * @brief Emit one ordered operation and its metadata origins.
   * @param op Verified operation to translate.
   * @param block Owning block containing any outgoing branch arms.
   * @param next Next physically fallthrough-reachable body label, if known.
   * @param local_begin Earliest position in this block sharing the source operation's incoming debugger state.
   */
  void operation(const SsaOperation& op, const SsaBlock& block, LabelId next, std::size_t local_begin);
  /**
   * @brief Emit a branch, switch, return or throw operation.
   * @param op Control operation whose operands are already on the stack.
   * @param block Owner of the outgoing edges.
   * @param next Physical fallthrough label, if known.
   * @param core Receives the position of the actual control instruction.
   */
  void control(const SsaOperation& op, const SsaBlock& block, LabelId next, std::size_t& core);
  /**
   * @brief Record all ordered exception clauses covering an emitted instruction.
   * @param op Original graph operation.
   * @param position Physical position of its potentially throwing instruction.
   */
  void protect(const SsaOperation& op, std::size_t position);
  /**
   * @brief Bind final positions, build the exception table and relocate dead-root frame types.
   */
  void finalize();
};

void Emitter::fail(SsaErrorKind kind, std::string reason) {
  if (!failure)
    failure = SsaError{kind, std::move(reason), current ? graph.operations[current.index].block : SsaBlockId{}, current};
}

void Emitter::account_staged_storage() {
  const auto now = commit.class_allocation_bytes();
  if (now > accounted_bytes) {
    memory.charge(now - accounted_bytes);
    accounted_bytes = now;
  }
}

bool Emitter::charge(std::size_t amount) {
  if (failure)
    return false;
  auto spent = budget.spend(amount);
  if (!spent)
    failure = std::move(spent.error());
  return !failure;
}

LabelId Emitter::label() {
  if (failure)
    return {};
  auto id = code.make_label();
  account_staged_storage();
  if (!id) {
    fail(SsaErrorKind::ResourceLimitExceeded, "could not allocate JVM label");
    return {};
  }
  return *id;
}

LabelId Emitter::boundary(std::size_t position) {
  const auto it = boundaries.find(position);
  if (it != boundaries.end())
    return it->second;
  const auto id = label();
  boundaries.emplace(position, id);
  bindings.emplace_back(id, position);
  return id;
}

InstructionId Emitter::emit(const InstructionBuilder& instruction) {
  if (!charge(1))
    return {};
  if (code.size() >= budget.limits.max_instructions) {
    fail(SsaErrorKind::ResourceLimitExceeded, "lowered method exceeds the instruction limit");
    return {};
  }
  const auto id = code.append(instruction);
  account_staged_storage();
  if (!id) {
    fail(SsaErrorKind::EmissionFailure, "could not encode SSA operation as a logical JVM instruction");
    return {};
  }
  return *id;
}

InstructionId Emitter::emit_simple(Opcode op) {
  const auto instruction = Instruction::simple(op);
  if (!instruction) {
    fail(SsaErrorKind::InvalidGraph, "SSA opcode needs an operand");
    return {};
  }
  return emit(*instruction);
}

void Emitter::reference(SsaValueId value) {
  if (!slots.carried[value.index]) {
    const auto slot = slots.homes[value.index];
    if (slot == SSA_NO_SLOT) {
      fail(SsaErrorKind::InvalidGraph, "SSA input has no JVM home at its use");
      return;
    }
    emit(access(graph.values[value.index].type.kind, static_cast<std::uint16_t>(slot), false));
  }
}

void Emitter::store_result(SsaValueId value) {
  if (slots.carried[value.index])
    return;
  const auto kind = graph.values[value.index].type.kind;
  if (slots.needed[value.index])
    emit(access(kind, static_cast<std::uint16_t>(slots.homes[value.index]), true));
  else
    emit(width(kind) == 2 ? Instruction::pop2() : Instruction::pop());
}

void Emitter::increment(const SsaOperation& op, std::size_t& core) {
  const auto operand = op.instruction.increment_value();
  if (!operand) {
    fail(SsaErrorKind::InvalidGraph, "iinc has no immediate");
    return;
  }
  const auto input = op.inputs.front();
  const auto home = slots.homes[input.index];
  if (home != SSA_NO_SLOT && home == slots.homes[op.result.index]) {
    exact[op.id.index] = emit(Instruction::increment(static_cast<std::uint16_t>(home), operand->amount));
    return;
  }
  // If the input is already on the stack, push the constant above it.
  // Otherwise push the immediate first, as a stack-carried prefix of iadd
  // that will retain the same schedule after the emitted code is lifted.
  emit(Instruction::sipush(operand->amount));
  reference(input);
  core = code.size();
  emit_simple(Opcode::IAdd);
  store_result(op.result);
}

void Emitter::add_move(SsaEdgeId edge, std::uint32_t from, SsaValueId destination) {
  const auto to = slots.homes[destination.index];
  if (to != SSA_NO_SLOT && to != from)
    moves[edge.index].push_back({from, to, move_kind(graph.values[destination.index].type.kind)});
}

bool Emitter::metadata_anchor(const SsaOperation& op) const {
  return op.origin && op.origin->index < metadata_anchors.size() && metadata_anchors[op.origin->index];
}

void Emitter::layout_blocks() {
  std::pmr::vector<SsaBlockId> forward(graph.blocks.size(), scratch);
  for (const auto& block : graph.blocks) {
    if (!block.id)
      continue;
    if (!charge(1 + block.incoming.size() + block.outgoing.size() + block.operations.size()))
      return;
    // The VM supplies the caught object on every incoming path. Consume it at
    // the first instruction of a shared handler whenever the copies agree.
    if (!block.incoming.empty()) {
      const auto first = block.incoming.front();
      bool common_handler = true;
      for (const auto id : block.incoming) {
        if (!charge(moves[id.index].size()))
          return;
        if (graph.edges[id.index].kind != SsaEdgeKind::Exceptional || moves[id.index] != moves[first.index]) {
          common_handler = false;
          break;
        }
      }
      if (common_handler)
        handler_entries[block.id.index] = first;
    }
    const auto& term = graph.operations[block.operations.back().index];
    // Evaluate control operands before copies. If all normal arms require the
    // same transfer, the copies can run immediately before the branch unless
    // a handler needs the locals as they were on entry to this instruction.
    SsaEdgeId normal{};
    bool common_transfer = true;
    for (auto id : block.outgoing) {
      const auto& edge = graph.edges[id.index];
      if (edge.kind == SsaEdgeKind::Exceptional) {
        if (edge.site == term.id)
          common_transfer = false;
      } else if (!normal) {
        normal = id;
      } else {
        if (!charge(moves[id.index].size()))
          return;
        if (edge.target != graph.edges[normal.index].target || moves[id.index] != moves[normal.index])
          common_transfer = false;
      }
    }
    if (!normal || !common_transfer)
      continue;
    if (!moves[normal.index].empty()) {
      predecessor_moves[block.id.index] = normal;
      continue;
    }
    // A lifted block may contain local loads or stack shuffles whose only
    // remaining purpose is provenance. Keep it if any origin anchors a
    // handler, debugger-visible assignment, or offset-bearing attribute.
    if (opcode_info(term.opcode).control_flow != ControlFlow::Jump || block.outgoing.size() != 1)
      continue;
    if (std::ranges::any_of(block.operations, [&](SsaOperationId id) {
          const auto& op = graph.operations[id.index];
          return (id != term.id && !op.anchor) || op.local_assignment || metadata_anchor(op);
        }))
      continue;
    const bool has_entry = std::ranges::any_of(block.incoming, [&](SsaEdgeId id) {
      const auto& edge = graph.edges[id.index];
      return !edge.source || edge.kind == SsaEdgeKind::Exceptional;
    });
    if (!has_entry)
      forward[block.id.index] = graph.edges[normal.index].target;
  }

  // Resolve each chain once. For a cycle, retain one real self-jump so its
  // incoming branches still target an instruction and execution stays in it.
  std::pmr::vector<bool> visited(graph.blocks.size(), false, scratch);
  std::pmr::vector<SsaBlockId> path(scratch);
  for (const auto& block : graph.blocks) {
    auto at = block.id;
    if (!at || block_labels[at.index].scope)
      continue;
    while (forward[at.index] && !visited[at.index]) {
      visited[at.index] = true;
      path.push_back(at);
      at = forward[at.index];
    }
    if (!block_labels[at.index].scope) {
      block_labels[at.index] = label();
      forward[at.index] = {};
    }
    for (auto id : path)
      block_labels[id.index] = block_labels[at.index];
    path.clear();
  }
  for (const auto& block : graph.blocks)
    if (block.id && !forward[block.id.index])
      bodies.push_back(block.id);
}

void Emitter::prepare() {
  for (const auto& value : graph.values) {
    if (!value.id)
      continue;
    if (!charge(1 + value.incoming.size()))
      return;
    if (value.kind == SsaValueKind::RootStack || value.kind == SsaValueKind::RootLocal)
      root_values[value.edge.index].push_back(value.id);
    if (value.kind == SsaValueKind::Exception && slots.capture_exception[value.id.index])
      add_move(value.edge, SSA_NO_SLOT, value.id);
    if (value.kind != SsaValueKind::Phi)
      continue;
    for (const auto& input : value.incoming) {
      const auto& src = graph.values[input.value.index];
      const bool caught = src.kind == SsaValueKind::Exception && src.edge == input.edge;
      add_move(input.edge, caught ? SSA_NO_SLOT : slots.homes[src.id.index], value.id);
    }
  }

  layout_blocks();
  // One pad can serve different throwing sites when both their live homes and handler agree.
  std::pmr::unordered_multimap<std::size_t, SsaEdgeId> unique(scratch);
  for (const auto& edge : graph.edges) {
    if (!edge.id)
      continue;
    auto& bundle = moves[edge.id.index];
    if (!charge(1 + bundle.size()))
      return;
    if (edge.kind == SsaEdgeKind::Entry || edge.kind == SsaEdgeKind::DeadRoot) {
      root_edges[edge.target.index].push_back(edge.id);
      continue;
    }
    const bool is_handler = edge.kind == SsaEdgeKind::Exceptional;
    if (is_handler)
      exceptional[edge.site.index].push_back(edge.id);
    if ((is_handler && handler_entries[edge.target.index]) ||
        (!is_handler && (bundle.empty() || predecessor_moves[edge.source.index]))) {
      targets[edge.id.index] = block_labels[edge.target.index];
      continue;
    }
    std::ranges::sort(bundle, {}, &Move::to);
    std::size_t hash = edge.target.index;
    const auto combine = [&](std::size_t v) { hash = hash * 16777619u + v; };
    combine(edge.handler.value_or(UINT32_MAX));
    for (const auto& move : bundle) {
      combine(move.from);
      combine(move.to);
      combine(static_cast<unsigned>(move.kind));
    }
    const auto [first, last] = unique.equal_range(hash);
    for (auto i = first; i != last; ++i) {
      if (!charge(1 + bundle.size()))
        return;
      const auto& other = graph.edges[i->second.index];
      if (edge.target == other.target && edge.handler == other.handler && moves[other.id.index] == bundle) {
        targets[edge.id.index] = targets[other.id.index];
        break;
      }
    }
    if (targets[edge.id.index].scope == 0) {
      targets[edge.id.index] = label();
      pad_edges.push_back(edge.id);
      unique.emplace(hash, edge.id);
    }
  }
}

void Emitter::copies(std::span<const Move> bundle, bool from_handler) {
  std::pmr::vector<Move> pending(bundle.begin(), bundle.end(), scratch);
  const auto limit = std::min<std::size_t>(UINT16_MAX, budget.limits.max_local_slots);
  if (from_handler) {
    const auto caught = std::ranges::find(pending, SSA_NO_SLOT, &Move::from);
    if (caught == pending.end()) {
      emit(Instruction::pop());
    } else {
      // Capture directly in the destination unless another copy still reads its
      // old contents. Other consumers can then copy from this same reference home.
      if (!charge(pending.size()))
        return;
      auto capture = caught->to;
      const bool still_read = std::ranges::any_of(pending, [&](const Move& move) {
        return move.from != SSA_NO_SLOT && overlap(move.from, width(move.kind), capture, 1);
      });
      if (!still_read) {
        pending.erase(caught);
      } else {
        if (caught_slot == SSA_NO_SLOT) {
          if (slots.count >= limit) {
            fail(SsaErrorKind::ResourceLimitExceeded, "caught exception requires a scratch local");
            return;
          }
          caught_slot = slots.count++;
        }
        capture = caught_slot;
      }
      emit(Instruction::astore(static_cast<std::uint16_t>(capture)));
      for (auto& move : pending)
        if (move.from == SSA_NO_SLOT)
          move.from = capture;
    }
  }
  if (pending.empty())
    return;
  const auto move_home = [&](const Move& move) {
    emit(access(move.kind, static_cast<std::uint16_t>(move.from), false));
    emit(access(move.kind, static_cast<std::uint16_t>(move.to), true));
  };
  if (pending.size() == 1) {
    move_home(pending.front());
    return;
  }

  // A ready move writes only slots whose old contents have no other readers.
  // Revisit the writers of the two affected slots whenever a read is removed.
  struct SlotActivity {
    std::size_t reads = 0;
    std::size_t writer = SIZE_MAX;
  };
  std::pmr::unordered_map<std::uint32_t, SlotActivity> activity(scratch);
  std::pmr::unordered_multimap<std::uint32_t, std::size_t> readers(scratch);
  std::pmr::vector<bool> done(pending.size(), false, scratch);
  std::pmr::vector<std::size_t> ready(scratch);
  std::pmr::vector<std::pair<std::uint32_t, unsigned>> temporaries(scratch);
  for (std::size_t i = 0; i < pending.size(); ++i) {
    const auto& move = pending[i];
    readers.emplace(move.from, i);
    for (unsigned n = 0; n < width(move.kind); ++n) {
      ++activity[move.from + n].reads;
      activity[move.to + n].writer = i;
    }
  }
  const auto enqueue = [&](std::size_t i) {
    if (i == SIZE_MAX || done[i])
      return;
    const auto& move = pending[i];
    for (unsigned n = 0; n < width(move.kind); ++n) {
      const auto to = move.to + n;
      const unsigned self = overlap(to, 1, move.from, width(move.kind)) ? 1 : 0;
      if (activity[to].reads > self)
        return;
    }
    ready.push_back(i);
  };
  const auto release = [&](std::uint32_t slot, unsigned size) {
    for (unsigned n = 0; n < size; ++n) {
      auto& state = activity[slot + n];
      --state.reads;
      enqueue(state.writer);
    }
  };
  for (std::size_t i = 0; i < pending.size(); ++i)
    enqueue(i);
  auto next_temp = slots.count;
  std::size_t remaining = pending.size();
  std::size_t candidate = 0;
  while (remaining) {
    if (!charge(1))
      return;
    if (!ready.empty()) {
      const auto i = ready.back();
      ready.pop_back();
      if (!done[i]) {
        done[i] = true;
        --remaining;
        const auto& move = pending[i];
        move_home(move);
        release(move.from, width(move.kind));
      }
      continue;
    }

    // Protect an original source, never a scratch source. The latter already
    // lies outside every destination and moving it cannot break another cycle.
    while (candidate < pending.size() && (done[candidate] || pending[candidate].from >= slots.count))
      ++candidate;
    if (candidate == pending.size()) {
      fail(SsaErrorKind::InvalidGraph, "inconsistent parallel-copy dependencies");
      return;
    }
    const auto saved = pending[candidate++];
    const auto saved_width = width(saved.kind);
    auto temp = next_temp;
    for (const auto& [slot, size] : temporaries) {
      if (!charge(1))
        return;
      if (size == saved_width && activity[slot].reads == 0) {
        temp = slot;
        break;
      }
    }
    if (temp == next_temp) {
      if (saved_width > limit - next_temp) {
        fail(SsaErrorKind::ResourceLimitExceeded, "parallel copy cycle exceeds the JVM local limit");
        return;
      }
      temporaries.emplace_back(temp, saved_width);
      next_temp += saved_width;
    }
    move_home({saved.from, temp, saved.kind});
    const auto [first, last] = readers.equal_range(saved.from);
    for (auto it = first; it != last; ++it) {
      if (!charge(1))
        return;
      auto& move = pending[it->second];
      if (done[it->second] || move.from != saved.from)
        continue;
      move.from = temp;
      for (unsigned n = 0; n < saved_width; ++n)
        ++activity[temp + n].reads;
      release(saved.from, saved_width);
    }
  }
}

void Emitter::root(const SsaEdge& edge) {
  root_positions[edge.id.index] = code.size();
  if (edge.kind == SsaEdgeKind::Entry && !debug_homes.empty()) {
    for (auto id : graph.parameters) {
      const auto& value = graph.values[id.index];
      if (!slots.needed[id.index] || slots.homes[id.index] == value.slot)
        continue;
      emit(access(value.type.kind, static_cast<std::uint16_t>(value.slot), false));
      emit(access(value.type.kind, static_cast<std::uint16_t>(slots.homes[id.index]), true));
    }
  }
  copies(moves[edge.id.index], false);
  // A disconnected seed is a verification-only predecessor. Backedges to the user block
  // must bypass its entry moves, even if there are no emitted real-entry predecessors.
  if (edge.kind == SsaEdgeKind::DeadRoot)
    emit(Branch{Opcode::Goto, block_labels[edge.target.index]});
}

void Emitter::control(const SsaOperation& op, const SsaBlock& block, LabelId next, std::size_t& core) {
  LabelId jump{}, other{};
  std::pmr::vector<LookupPair> cases(scratch);
  if (const auto edge = predecessor_moves[block.id.index])
    copies(moves[edge.index], false);
  for (auto id : block.outgoing) {
    const auto& edge = graph.edges[id.index];
    switch (edge.kind) {
      case SsaEdgeKind::Conditional:
      case SsaEdgeKind::Unconditional:
        jump = targets[id.index];
        break;
      case SsaEdgeKind::Fallthrough:
      case SsaEdgeKind::SwitchDefault:
        other = targets[id.index];
        break;
      case SsaEdgeKind::SwitchCase:
        cases.push_back({edge.case_key.value_or(0), targets[id.index]});
        break;
      default:
        break;
    }
  }
  core = code.size();
  switch (opcode_info(op.opcode).control_flow) {
    case ControlFlow::Jump: {
      const auto target = jump.scope ? jump : other;
      if (target != next || !exceptional[op.id.index].empty() || metadata_anchor(op))
        exact[op.id.index] = emit(Branch{Opcode::Goto, target});
      break;
    }
    case ControlFlow::ConditionalBranch:
      exact[op.id.index] = emit(Branch{op.opcode, jump});
      if (other != next)
        emit(Branch{Opcode::Goto, other});
      break;
    case ControlFlow::Switch:
      std::ranges::sort(cases, {}, &LookupPair::key);
      if (op.opcode == Opcode::TableSwitch) {
        if (cases.empty()) {
          fail(SsaErrorKind::InvalidGraph, "tableswitch requires a case");
          return;
        }
        std::pmr::vector<LabelId> labels(scratch);
        for (const auto& item : cases)
          labels.push_back(item.target);
        exact[op.id.index] = emit(TableSwitch{cases.front().key, other, labels});
      } else {
        exact[op.id.index] = emit(LookupSwitch{other, cases});
      }
      break;
    default:
      exact[op.id.index] = emit_simple(op.opcode);
      break;
  }
}

void Emitter::protect(const SsaOperation& op, std::size_t position) {
  for (auto id : exceptional[op.id.index]) {
    const auto& edge = graph.edges[id.index];
    // Graph verification established the handler ordinal of every exceptional edge.
    auto& segments = ranges[*edge.handler]; // NOLINT(bugprone-unchecked-optional-access)
    if (!segments.empty() && segments.back().end == position && segments.back().target == targets[id.index])
      segments.back().end = position + 1;
    else
      segments.push_back({position, position + 1, targets[id.index]});
  }
}

void Emitter::operation(const SsaOperation& op, const SsaBlock& block, LabelId next, std::size_t local_begin) {
  if (!charge(1 + op.inputs.size() + exceptional[op.id.index].size()))
    return;
  current = op.id;
  const auto begin = code.size();
  auto core = begin;
  const auto flow = opcode_info(op.opcode).control_flow;
  if (op.anchor) {
    if (!exceptional[op.id.index].empty())
      emit(Instruction::nop());
  } else if (op.opcode == Opcode::IInc) {
    increment(op, core);
  } else {
    const bool constructor_result = op.opcode == Opcode::InvokeSpecial && op.result && !op.inputs.empty() &&
                                    (graph.values[op.inputs.front().index].type.kind == Kind::Uninitialized ||
                                     graph.values[op.inputs.front().index].type.kind == Kind::UninitializedThis);
    for (std::size_t i = 0; i < op.inputs.size(); ++i) {
      reference(op.inputs[i]);
      if (i == 0 && constructor_result && slots.needed[op.result.index])
        emit(Instruction::dup());
    }
    core = code.size();
    if (flow != ControlFlow::Next) {
      control(op, block, next, core);
    } else if (op.symbolic) {
      auto instruction = commit.resolve(*op.symbolic);
      if (!instruction) {
        fail(SsaErrorKind::EmissionFailure, "could not resolve symbolic SSA operand in the destination class");
        return;
      }
      exact[op.id.index] = emit(*instruction);
    } else {
      exact[op.id.index] = emit(op.instruction);
    }
    if (op.result && (slots.needed[op.result.index] || !constructor_result))
      store_result(op.result);
  }
  if (!exceptional[op.id.index].empty())
    protect(op, core);
  if (op.local_assignment && has_debug_home(op.local_assignment->slot)) {
    const auto id = op.local_assignment->value;
    reference(id);
    const auto store = access(graph.values[id.index].type.kind, static_cast<std::uint16_t>(debug_homes[op.local_assignment->slot]), true);
    const auto at = emit(store);
    if (op.origin && op.anchor && op.instruction.opcode() == store.opcode())
      exact[op.id.index] = at;
  }
  if (op.origin) {
    const auto matched = exact[op.id.index];
    provenance.instructions.push_back({*op.origin, boundary(begin), boundary(code.size()),
                                       matched.generation ? std::optional{matched} : std::nullopt,
                                       local_begin != begin ? boundary(local_begin) : LabelId{}});
  }
}

void Emitter::finalize() {
  for (std::size_t ordinal = 0; ordinal < ranges.size(); ++ordinal) {
    for (const auto& range : ranges[ordinal]) {
      if (!charge(1))
        return;
      if (handlers.size() >= std::min<std::size_t>(UINT16_MAX, budget.limits.max_handlers)) {
        fail(SsaErrorKind::ResourceLimitExceeded, "lowered exception table exceeds the handler limit");
        return;
      }
      handlers.push_back({boundary(range.begin), boundary(range.end), range.target, graph.handlers[ordinal].catch_type});
      provenance.handler_sources.push_back(graph.handlers[ordinal].origin);
    }
  }
  for (const auto& [id, position] : bindings) {
    const auto at = position < code.size() ? CodePosition{code.at(position)} : CodePosition{};
    if (!cafe::detail::BytecodeAccess::bind_initial_label(code, id, at)) {
      fail(SsaErrorKind::EmissionFailure, "could not bind staged control-flow boundary");
      return;
    }
  }
  for (const auto& edge : graph.edges) {
    if (!edge.id || edge.kind != SsaEdgeKind::DeadRoot)
      continue;
    auto& frame = seeds.emplace_back(StackMapFrame{code.at(root_positions[edge.id.index]), Frame(scratch)}).frame;
    frame.constructor_uninitialized = edge.constructor_uninitialized;
    const auto add = [&](std::uint32_t home, const SsaType& type) {
      if (home == SSA_NO_SLOT)
        return;
      const auto end = home + type.width();
      if (frame.locals.size() < end)
        frame.locals.resize(end);
      frame.locals[home] = {type.kind, type.reference, type.allocation ? exact[type.allocation.index] : InstructionId{}};
      if (type.width() == 2)
        frame.locals[home + 1] = {Kind::WideContinuation};
    };
    for (auto id : root_values[edge.id.index]) {
      const auto& value = graph.values[id.index];
      add(slots.homes[id.index], value.type);
      if (value.kind == SsaValueKind::RootLocal && has_debug_home(value.slot))
        add(debug_homes[value.slot], value.type);
    }
  }
}

SsaResult<SsaLowerResult> Emitter::run(AnalysisContext& context, const SsaLowerOptions& options) {
  prepare();
  for (std::size_t index = 0; !failure && index < bodies.size(); ++index) {
    const auto& block = graph.blocks[bodies[index].index];
    for (auto id : root_edges[block.id.index])
      root(graph.edges[id.index]);
    bindings.emplace_back(block_labels[block.id.index], code.size());
    if (const auto handler = handler_entries[block.id.index])
      copies(moves[handler.index], true);
    LabelId next{};
    if (index + 1 < bodies.size() && root_edges[bodies[index + 1].index].empty())
      next = block_labels[bodies[index + 1].index];
    // Inserted operations can read and write SSA homes, but never touch a
    // dedicated debug home. Include them in the next source operation's local
    // coverage while preserving its exact line and annotation coordinates.
    auto local_begin = code.size();
    for (auto id : block.operations) {
      const auto& op = graph.operations[id.index];
      operation(op, block, next, local_begin);
      if (op.origin)
        local_begin = code.size();
    }
  }
  current = {};
  for (auto id : pad_edges) {
    if (failure)
      break;
    const auto& edge = graph.edges[id.index];
    bindings.emplace_back(targets[id.index], code.size());
    copies(moves[id.index], edge.kind == SsaEdgeKind::Exceptional);
    emit(Branch{Opcode::Goto, block_labels[edge.target.index]});
  }
  // Each debugger home is dedicated for the method. Relocation intersects these
  // verified homes with the emitted fragments of each individual source range.
  if (!failure && !debug_homes.empty() && source) {
    std::pmr::vector<bool> intact(debug_homes.size(), true, scratch);
    std::pmr::unordered_map<std::uint32_t, InstructionId> writes(scratch);
    for (const auto& op : graph.operations)
      if (op.id && op.origin && op.local_assignment)
        writes.emplace(op.origin->index, *op.origin);
    for (const auto& entry : source->code().instructions()) {
      const auto opcode = entry.instruction.opcode();
      if ((opcode < Opcode::IStore || opcode > Opcode::AStore) && opcode != Opcode::IInc)
        continue;
      const auto slot = opcode == Opcode::IInc ? entry.instruction.increment_value()->local : *entry.instruction.local_index();
      if (!has_debug_home(slot))
        continue;
      const auto found = writes.find(entry.id.index);
      if (found == writes.end() || found->second != entry.id)
        intact[slot] = false;
    }
    const auto begin = boundary(0);
    const auto end = boundary(code.size());
    for (std::size_t slot = 0; slot < debug_homes.size(); ++slot)
      if (intact[slot] && has_debug_home(slot))
        provenance.locals.push_back({static_cast<std::uint16_t>(slot), static_cast<std::uint16_t>(debug_homes[slot]), begin, end});
  }
  if (!failure)
    finalize();
  if (failure)
    return std::unexpected(std::move(*failure));
  auto limits = context.limits();
  limits.upstream_resource = &memory;
  limits.max_work = budget.remaining;
  AnalysisContext final_context(context.hierarchy(), limits);
  auto installed = commit.finish(graph.source_stamp, final_context, handlers, seeds, provenance, options.drop_unsafe_code_attributes);
  if (!installed)
    return std::unexpected(write_failure(installed.error()));
  return SsaLowerResult{std::move(*installed)};
}
} // namespace

SsaResult<SsaLowerResult> lower_ssa(ClassFile& owner, const SsaMethod& method, AnalysisContext& context,
                                    const SsaLowerOptions& options) try {
  auto verified = verify_ssa(owner, method, context);
  if (!verified)
    return std::unexpected(std::move(verified.error()));
  const auto& graph = SsaAccess::get(method);
  const auto* source = owner.methods()[graph.method_index].code();
  const auto& limits = context.limits();
  detail::BudgetResource memory(limits.upstream_resource, limits.max_memory_bytes);
  SsaLowerBudget work{limits, limits.max_work};
  std::pmr::vector<std::uint8_t> widths(&memory), metadata_anchors(&memory);
  if (source) {
    auto collected = detail::ssa_debug_slot_widths(owner, *source, limits, work.remaining, &memory, &metadata_anchors);
    if (!collected)
      return std::unexpected(write_failure(collected.error()));
    widths = std::move(*collected);
  }
  std::size_t parameters = 0;
  for (const auto id : graph.parameters) {
    const auto& value = graph.values[id.index];
    parameters = std::max<std::size_t>(parameters, value.slot + value.type.width());
  }
  // Some slots hold different categories in disjoint scopes. Include every
  // mirrored write's width, even when only a narrow scope names this slot.
  if (!widths.empty()) {
    if (auto charged = work.spend(graph.operations.size() + graph.values.size() + widths.size()); !charged)
      return std::unexpected(std::move(charged.error()));
    for (const auto& op : graph.operations) {
      if (op.id && op.local_assignment) {
        const auto [slot, id] = *op.local_assignment;
        if (slot < widths.size() && widths[slot])
          widths[slot] = std::max<std::uint8_t>(widths[slot], graph.values[id.index].type.width());
      }
    }
    for (const auto& value : graph.values)
      if (value.id && value.kind == SsaValueKind::RootLocal && value.slot < widths.size() && widths[value.slot])
        widths[value.slot] = std::max<std::uint8_t>(widths[value.slot], value.type.width());
  }
  std::pmr::vector<std::uint32_t> homes(widths.size(), SSA_NO_SLOT, &memory);
  std::size_t reserved = parameters;
  for (std::size_t slot = 0; slot < widths.size(); ++slot) {
    if (!widths[slot])
      continue;
    const auto home = slot < parameters ? slot : reserved;
    homes[slot] = static_cast<std::uint32_t>(home);
    reserved = std::max(reserved, home + widths[slot]);
  }
  auto slots = assign_ssa_slots(graph, reserved, homes, work, &memory);
  if (!slots)
    return std::unexpected(std::move(slots.error()));
  detail::SsaCommit commit(owner, graph.method_index);
  Emitter emitter(graph, source, homes, reserved, metadata_anchors, *slots, commit, work, memory);
  return emitter.run(context, options);
} catch (const detail::BudgetExceeded&) {
  return std::unexpected(SsaError{SsaErrorKind::ResourceLimitExceeded, "SSA lowering memory budget exhausted"});
}
} // namespace cafe::analysis
