/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#include "cafe/analysis/control_flow_graph.hpp"

#include <algorithm>
#include <bit>
#include <numeric>
#include <unordered_map>

#include "bytecode/code_access.hpp"
#include "cafe/class_version.hpp"
#include "model/budget_resource.hpp"

namespace cafe::analysis {
namespace {

constexpr unsigned LEADER = 1; ///< Instruction-boundary bit marking a basic-block leader.
constexpr unsigned FRAME_BOUNDARY = 2; ///< Instruction-boundary bit marking a possible emitted verifier frame.

/**
 * @brief Pack an instruction slot and generation for sparse CFG lookup.
 * @param id Stable instruction handle belonging to the supplied method.
 * @return A unique 64-bit handle key.
 * @ingroup implementation
 */
[[nodiscard]] std::uint64_t key(InstructionId id) noexcept {
  return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
}

/**
 * @brief Normal successor staged with its source/target instruction-order positions.
 * @ingroup implementation
 */
struct PlannedEdge {
  std::uint32_t from; ///< Source instruction-order position of a planned edge or original branch.
  std::uint32_t to; ///< Destination instruction-order position or original branch offset.
  EdgeKind kind; ///< Discriminator selecting the interpretation of this record.
  std::optional<std::int32_t> case_key{}; ///< Signed key for a SwitchCase edge; empty for other edge kinds.
};

/**
 * @brief Exception-table interval staged before graph range-index allocation.
 * @ingroup implementation
 */
struct PlannedRange {
  std::uint32_t begin; ///< Inclusive instruction-order position.
  std::uint32_t end; ///< Exclusive instruction-order position.
  std::uint32_t target; ///< Scoped destination instruction boundary or graph block.
};

/**
 * @brief Instruction generation and order position for dense CFG handle lookup.
 * @ingroup implementation
 */
struct SlotPosition {
  std::uint32_t generation = 0; ///< Nonzero handle generation; changes when a slot is reused.
  std::uint32_t position = 0; ///< Current index in method instruction order.
};

/**
 * @brief Visit the disjoint segment-tree nodes covering a half-open interval.
 * @param leaf_count Power-of-two leaf capacity of the exception-range segment tree.
 * @param first Inclusive lower boundary of the processed interval.
 * @param end Exclusive upper boundary of the processed interval.
 * @param visit Callback invoked for each node in the disjoint segment-tree cover.
 * @ingroup implementation
 */
template<typename Visitor>
void visit_cover(std::size_t leaf_count, std::size_t first, std::size_t end, Visitor&& visit) {
  first += leaf_count;
  end += leaf_count;
  while (first < end) {
    if (first % 2 != 0)
      visit(first++);
    if (end % 2 != 0)
      visit(--end);
    first /= 2;
    end /= 2;
  }
}

/**
 * @brief Construct a CFG failure with optional instruction and label context.
 * @param kind Payload or error discriminator.
 * @param detail Explanation copied into the structured error.
 * @param instruction Instruction handle belonging to the analyzed method.
 * @param label Optional scoped label associated with a failure.
 * @return An unexpected AnalysisError.
 * @ingroup implementation
 */
[[nodiscard]] std::unexpected<AnalysisError> graph_error(
    AnalysisErrorKind kind, const char* detail, std::optional<InstructionId> instruction = {},
    std::optional<LabelId> label = {}) {
  return std::unexpected(AnalysisError{.kind = kind, .instruction = instruction, .label = label, .detail = detail});
}

} // namespace

/**
 * @brief Budgeted block/edge arrays, dense or sparse handle lookup, and a segment-tree index for
 * exception ranges.
 * @ingroup implementation
 */
struct ControlFlowGraph::Storage {
  /**
   * @brief Initialize budgeted block/edge arrays, dense or sparse handle lookup, and a segment-tree
   * index for exception ranges.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @param limits Resource ceilings and cooperative cancellation policy.
   */
  explicit Storage(const MethodCode& code, std::pmr::memory_resource* resource, const AnalysisLimits& limits) :
      budget(resource, limits.max_memory_bytes), block_list(&budget), edges(&budget), outgoing_offsets(&budget),
      ranges(&budget), dense_positions(&budget), sparse_positions(&budget), block_ids(&budget),
      cover_offsets(&budget), cover_ranges(&budget), scope(detail::BytecodeAccess::scope(code)),
      revision(code.revision()) {
  }

  /**
   * @brief Resolve a stable instruction handle through dense or sparse indexing.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return Its method-order position, or nullopt when unrecognized.
   */
  [[nodiscard]] std::optional<std::uint32_t> position(InstructionId id) const {
    if (id.generation == 0)
      return std::nullopt;
    if (!dense_positions.empty()) {
      if (id.index < dense_positions.size() && dense_positions[id.index].generation == id.generation)
        return dense_positions[id.index].position;
      return std::nullopt;
    }
    const auto found = sparse_positions.find(key(id));
    return found == sparse_positions.end() ? std::nullopt : std::optional{found->second};
  }

  detail::BudgetResource budget; ///< Owning forwarding resource enforcing charged allocation bytes.
  std::pmr::vector<BasicBlock> block_list; ///< Basic blocks in instruction order.
  std::pmr::vector<Edge> edges; ///< Normal CFG successors grouped by source block.
  std::pmr::vector<std::size_t> outgoing_offsets; ///< Offsets delimiting each block's slice of the normal edge array.
  std::pmr::vector<ProtectedRange> ranges; ///< Protected exception-table intervals retained in table order.
  std::pmr::vector<SlotPosition> dense_positions; ///< Dense slot-index table with generation checks and method-order positions.
  std::pmr::unordered_map<std::uint64_t, std::uint32_t> sparse_positions; ///< Sparse generation/slot-to-position lookup for heavily edited methods.
  std::pmr::vector<BlockId> block_ids; ///< Block identity for each method-order instruction position.
  /**
   * @brief Segment-tree node offsets delimiting slices of covered exception-range indices.
   *
   * Each range is stored in a disjoint cover of segment tree nodes. Each node
   * owns a slice of cover_ranges, and point queries walk one ancestor path.
   */
  std::pmr::vector<std::size_t> cover_offsets;
  std::pmr::vector<std::uint32_t> cover_ranges; ///< Exception-range indices stored in disjoint segment-tree node covers.
  std::size_t leaf_count = 0; ///< Power-of-two leaf capacity of the protected-range index.
  std::uint32_t scope; ///< Method identity used to reject foreign labels or switches.
  std::uint64_t revision; ///< Instruction revision captured when this metadata was supplied.
};

ControlFlowGraph::ControlFlowGraph(std::unique_ptr<Storage> storage) noexcept : storage(std::move(storage)) {
}
ControlFlowGraph::~ControlFlowGraph() = default;
ControlFlowGraph::ControlFlowGraph(ControlFlowGraph&&) noexcept = default;
ControlFlowGraph& ControlFlowGraph::operator=(ControlFlowGraph&&) noexcept = default;

std::span<const BasicBlock> ControlFlowGraph::blocks() const noexcept {
  return storage ? std::span<const BasicBlock>(storage->block_list) : std::span<const BasicBlock>{};
}

std::span<const Edge> ControlFlowGraph::outgoing(BlockId from) const noexcept {
  if (!storage || from.value >= storage->block_list.size())
    return {};
  const auto begin = storage->outgoing_offsets[from.value];
  return std::span<const Edge>(storage->edges).subspan(begin, storage->outgoing_offsets[from.value + 1] - begin);
}

std::optional<BlockId> ControlFlowGraph::block_of(InstructionId instruction) const {
  if (!storage)
    return std::nullopt;
  const auto position = storage->position(instruction);
  return position ? std::optional{storage->block_ids[*position]} : std::nullopt;
}

std::span<const ProtectedRange> ControlFlowGraph::exception_ranges() const noexcept {
  return storage ? std::span<const ProtectedRange>(storage->ranges) : std::span<const ProtectedRange>{};
}

void ControlFlowGraph::handlers_for(BlockId block, std::pmr::vector<std::uint32_t>& ranges) const {
  ranges.clear();
  if (!storage || storage->leaf_count == 0 || block.value >= storage->block_list.size())
    return;
  const auto covered = std::span<const std::uint32_t>(storage->cover_ranges);
  for (auto node = storage->leaf_count + block.value; node != 0; node /= 2) {
    const auto offset = storage->cover_offsets[node];
    const auto records = covered.subspan(offset, storage->cover_offsets[node + 1] - offset);
    if (!records.empty())
      ranges.insert(ranges.end(), records.begin(), records.end());
  }
  std::ranges::sort(ranges);
}

std::uint64_t ControlFlowGraph::code_revision() const noexcept {
  return storage ? storage->revision : 0;
}

bool ControlFlowGraph::is_current(const MethodCode& code, std::span<const ExceptionHandler> handlers) const noexcept {
  return storage && storage->scope == detail::BytecodeAccess::scope(code) && code_revision() == code.revision() &&
         std::ranges::equal(storage->ranges, handlers, {}, &ProtectedRange::handler);
}

std::expected<ControlFlowGraph, AnalysisError> build_cfg(
    const MethodCode& code, std::span<const ExceptionHandler> handlers,
    std::pmr::memory_resource* resource, const AnalysisLimits& limits) try {
  const auto count = code.size();
  if (limits.cancellation.stop_requested())
    return graph_error(AnalysisErrorKind::Cancelled, "control-flow construction cancelled");
  if (count == 0)
    return graph_error(AnalysisErrorKind::InvalidCode, "Code must contain at least one instruction");
  if (count > MAX_CODE_SIZE || handlers.size() > UINT16_MAX ||
      count > limits.max_instructions || handlers.size() > limits.max_handlers || count > limits.max_work)
    return graph_error(AnalysisErrorKind::ResourceLimitExceeded, "instruction or exception handler limit exceeded");

  ControlFlowGraph graph(std::make_unique<ControlFlowGraph::Storage>(code, resource, limits));
  auto& data = *graph.storage;
  // Parsed and normally edited methods have dense slots. Bound the cost of
  // the dense table for methods whose edits left many free slots.
  const auto slots = detail::BytecodeAccess::slot_count(code);
  if (slots <= 2 * count)
    data.dense_positions.resize(slots);
  else
    data.sparse_positions.reserve(count);
  std::uint32_t position = 0;
  for (const auto& entry : code.instructions()) {
    if (!data.dense_positions.empty())
      data.dense_positions[entry.id.index] = {entry.id.generation, position};
    else
      data.sparse_positions.emplace(key(entry.id), position);
    ++position;
  }

  const auto resolve_label = [&](LabelId label, bool allow_end) -> std::optional<std::uint32_t> {
    const auto binding = code.label_position(label);
    if (!binding)
      return std::nullopt;
    if (!binding->before)
      return allow_end ? std::optional{static_cast<std::uint32_t>(count)} : std::nullopt;
    return data.position(*binding->before);
  };

  std::pmr::vector<unsigned> leaders(count + 1, 0, &data.budget);
  leaders.front() = LEADER;
  leaders.back() = LEADER;
  std::pmr::vector<PlannedEdge> planned_edges(&data.budget);
  const auto plan_target = [&](std::uint32_t from, InstructionId at, LabelId label,
                               EdgeKind kind, std::optional<std::int32_t> case_key = {})
      -> std::expected<void, AnalysisError> {
    const auto target = resolve_label(label, false);
    if (!target)
      return graph_error(AnalysisErrorKind::InvalidLabelOrRange, "invalid or unbound branch target", at, label);
    if (planned_edges.size() >= limits.max_edges || planned_edges.size() >= limits.max_work - count)
      return graph_error(AnalysisErrorKind::ResourceLimitExceeded, "control-flow edge limit exceeded", at);
    leaders[*target] |= LEADER | FRAME_BOUNDARY;
    planned_edges.push_back({from, *target, kind, case_key});
    return {};
  };

  position = 0;
  for (const auto& entry : code.instructions()) {
    const auto at = position++;
    if (limits.cancellation.stop_requested())
      return graph_error(AnalysisErrorKind::Cancelled, "control-flow construction cancelled", entry.id);
    const auto& info = opcode_info(entry.instruction.opcode());
    if (info.form == OperandForm::Illegal || info.form == OperandForm::WidePrefix ||
        info.logical_opcode != entry.instruction.opcode())
      return graph_error(AnalysisErrorKind::InvalidCode, "invalid logical opcode", entry.id);
    const auto flow = info.control_flow;
    if (flow == ControlFlow::Next)
      continue;
    if (flow == ControlFlow::SubroutineCall || flow == ControlFlow::SubroutineReturn)
      return graph_error(AnalysisErrorKind::UnsupportedSubroutineAnalysis, "jsr and ret require legacy subroutine analysis",
                         entry.id);
    if (position < count)
      leaders[position] |= flow == ControlFlow::ConditionalBranch ? LEADER : LEADER | FRAME_BOUNDARY;

    if (flow == ControlFlow::ConditionalBranch || flow == ControlFlow::Jump) {
      const auto target = code.branch_target(entry.id);
      if (!target)
        return graph_error(AnalysisErrorKind::InvalidLabelOrRange, "invalid branch operand", entry.id);
      const auto kind = flow == ControlFlow::Jump ? EdgeKind::Unconditional : EdgeKind::Conditional;
      if (auto added = plan_target(at, entry.id, *target, kind); !added)
        return std::unexpected(std::move(added.error()));
    }
    if (flow != ControlFlow::Switch)
      continue;

    const auto switch_id = code.switch_id(entry.id);
    if (!switch_id)
      return graph_error(AnalysisErrorKind::InvalidCode, "invalid switch operand", entry.id);
    if (info.form == OperandForm::TableSwitch) {
      const auto table = code.table_switch(*switch_id);
      if (!table || table->targets.empty() || table->targets.size() > MAX_CODE_SIZE ||
          static_cast<std::int64_t>(table->low) + static_cast<std::int64_t>(table->targets.size()) - 1 > INT32_MAX)
        return graph_error(AnalysisErrorKind::InvalidCode, "invalid tableswitch key range", entry.id);
      if (auto added = plan_target(at, entry.id, table->default_target, EdgeKind::SwitchDefault); !added)
        return std::unexpected(std::move(added.error()));
      for (std::size_t i = 0; i < table->targets.size(); ++i) {
        const auto case_key = static_cast<std::int32_t>(static_cast<std::int64_t>(table->low) + static_cast<std::int64_t>(i));
        if (auto added = plan_target(at, entry.id, table->targets[i], EdgeKind::SwitchCase, case_key); !added)
          return std::unexpected(std::move(added.error()));
      }
    } else {
      const auto table = code.lookup_switch(*switch_id);
      if (!table || table->pairs.size() > MAX_CODE_SIZE)
        return graph_error(AnalysisErrorKind::InvalidCode, "invalid lookupswitch payload", entry.id);
      if (auto added = plan_target(at, entry.id, table->default_target, EdgeKind::SwitchDefault); !added)
        return std::unexpected(std::move(added.error()));
      std::optional<std::int32_t> previous;
      for (const auto& pair : table->pairs) {
        if (previous && *previous >= pair.key)
          return graph_error(AnalysisErrorKind::InvalidCode, "lookupswitch keys must be strictly increasing", entry.id);
        previous = pair.key;
        if (auto added = plan_target(at, entry.id, pair.target, EdgeKind::SwitchCase, pair.key); !added)
          return std::unexpected(std::move(added.error()));
      }
    }
  }

  std::pmr::vector<PlannedRange> planned_ranges(&data.budget);
  planned_ranges.reserve(handlers.size());
  for (const auto& handler : handlers) {
    const auto begin = resolve_label(handler.start, false);
    const auto end = resolve_label(handler.end, true);
    const auto target = resolve_label(handler.handler, false);
    if (!begin || !end || *begin >= *end)
      return graph_error(AnalysisErrorKind::InvalidLabelOrRange, "invalid exception handler start or end",
                         {}, begin ? handler.end : handler.start);
    if (!target)
      return graph_error(AnalysisErrorKind::InvalidLabelOrRange, "invalid exception handler target", {}, handler.handler);
    leaders[*begin] |= LEADER;
    leaders[*end] |= LEADER;
    leaders[*target] |= LEADER | FRAME_BOUNDARY;
    planned_ranges.push_back({*begin, *end, *target});
  }

  const auto block_count = static_cast<std::size_t>(std::ranges::count_if(
      std::span<const unsigned>(leaders).first(count), [](unsigned flags) { return (flags & LEADER) != 0; }));
  data.block_list.reserve(block_count);
  data.outgoing_offsets.reserve(block_count + 1);
  data.block_ids.resize(count + 1);
  std::uint32_t from = 0;
  for (std::uint32_t end = 1; end <= count; ++end) {
    if ((leaders[end] & LEADER) == 0)
      continue;
    const auto block = BlockId{static_cast<std::uint32_t>(data.block_list.size())};
    data.block_list.push_back({code.at(from), code.at(end - 1), from, end,
                               (leaders[from] & FRAME_BOUNDARY) != 0, false});
    std::fill(data.block_ids.begin() + from, data.block_ids.begin() + end, block);
    from = end;
  }
  data.block_ids.back() = BlockId{static_cast<std::uint32_t>(block_count)};

  const auto edge_limit = std::min(limits.max_edges, limits.max_work - count);
  data.edges.reserve(std::min(edge_limit, planned_edges.size() + block_count));
  std::size_t next_planned = 0;
  for (const auto& block : data.block_list) {
    data.outgoing_offsets.push_back(data.edges.size());
    while (next_planned < planned_edges.size() && planned_edges[next_planned].from < block.end) {
      const auto& edge = planned_edges[next_planned++];
      data.edges.push_back({data.block_ids[edge.to], edge.kind, edge.case_key});
    }
    const auto flow = opcode_info(code.get(block.last)->opcode()).control_flow;
    if (block.end < count && (flow == ControlFlow::Next || flow == ControlFlow::ConditionalBranch))
      data.edges.push_back({data.block_ids[block.end], EdgeKind::Fallthrough});
    if (data.edges.size() > edge_limit)
      return graph_error(AnalysisErrorKind::ResourceLimitExceeded, "control-flow edge limit exceeded", block.last);
  }
  data.outgoing_offsets.push_back(data.edges.size());
  std::size_t logical_edges = data.edges.size();
  data.ranges.reserve(handlers.size());
  for (std::size_t i = 0; i < planned_ranges.size(); ++i) {
    const auto& range = planned_ranges[i];
    const auto covered_blocks = data.block_ids[range.end].value - data.block_ids[range.begin].value;
    if (covered_blocks > edge_limit - logical_edges)
      return graph_error(AnalysisErrorKind::ResourceLimitExceeded, "exceptional edge limit exceeded", {}, handlers[i].handler);
    logical_edges += covered_blocks;
    data.ranges.push_back({range.begin, range.end, data.block_ids[range.target], handlers[i]});
  }

  if (!data.ranges.empty()) {
    data.leaf_count = std::bit_ceil(block_count);
    data.cover_offsets.resize(2 * data.leaf_count + 1);
    const auto cover_range = [&](const ProtectedRange& range, auto&& visit) {
      visit_cover(data.leaf_count, data.block_ids[range.begin].value, data.block_ids[range.end].value, visit);
    };
    for (const auto& range : data.ranges)
      cover_range(range, [&](auto node) { ++data.cover_offsets[node + 1]; });
    std::partial_sum(data.cover_offsets.begin(), data.cover_offsets.end(), data.cover_offsets.begin());
    data.cover_ranges.resize(data.cover_offsets.back());
    std::pmr::vector<std::size_t> next(data.cover_offsets, &data.budget);
    for (std::uint32_t i = 0; i < data.ranges.size(); ++i)
      cover_range(data.ranges[i], [&](auto node) { data.cover_ranges[next[node]++] = i; });
  }

  std::pmr::vector<BlockId> pending(&data.budget);
  std::pmr::vector<std::uint32_t> active_handlers(&data.budget);
  const auto reach = [&](BlockId block) {
    if (!data.block_list[block.value].reachable) {
      data.block_list[block.value].reachable = true;
      pending.push_back(block);
    }
  };
  reach(BlockId{0});
  while (!pending.empty()) {
    const auto block_id = pending.back();
    pending.pop_back();
    const auto& block = data.block_list[block_id.value];
    if (limits.cancellation.stop_requested())
      return graph_error(AnalysisErrorKind::Cancelled, "control-flow construction cancelled", block.first);
    const auto flow = opcode_info(code.get(block.last)->opcode()).control_flow;
    if (block.end == count && (flow == ControlFlow::Next || flow == ControlFlow::ConditionalBranch))
      return graph_error(AnalysisErrorKind::InvalidCode, "reachable instruction falls off the end of Code", block.last);
    for (const auto& edge : graph.outgoing(block_id))
      reach(edge.target);
    graph.handlers_for(block_id, active_handlers);
    for (const auto handler : active_handlers)
      reach(data.ranges[handler].target);
  }
  return graph;
} catch (const detail::BudgetExceeded&) {
  return graph_error(AnalysisErrorKind::ResourceLimitExceeded, "control-flow memory budget exhausted");
}

} // namespace cafe::analysis
