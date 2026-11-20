/**
 * @file
 * @brief Bounded SSA liveness and deterministic width-aware linear scan allocation.
 * @ingroup implementation
 */
#include "ssa_slots.hpp"

#include <algorithm>
#include <queue>
#include <tuple>

namespace cafe::analysis {
namespace {

/// A use at a specific program point in a graph block.
struct Point {
  SsaBlockId block; ///< Block containing this use.
  std::size_t position; ///< Linear position before or after the source instruction.
};

/// Inclusive conservative live interval over block and operation positions.
struct Interval {
  std::size_t first; ///< Earliest occupied position.
  std::size_t last; ///< Last position where the home must remain intact.
  std::uint32_t value; ///< Index of the assigned SSA definition.
};

} // namespace

SsaResult<SsaSlots> assign_ssa_slots(const SsaStorage& graph, std::size_t reserved, bool mirror_locals,
                                     SsaLowerBudget& budget, std::pmr::memory_resource* scratch) {
  SsaSlots slots(scratch);
  slots.homes.assign(graph.values.size(), SSA_NO_SLOT);
  slots.carried.resize(graph.values.size());
  slots.needed.resize(graph.values.size());
  slots.capture_exception.resize(graph.values.size());
  const auto limit = std::min<std::size_t>(UINT16_MAX, budget.limits.max_local_slots);
  if (reserved > limit)
    return std::unexpected(SsaError{SsaErrorKind::ResourceLimitExceeded, "reserved local slots exceed the method limit"});
  slots.count = static_cast<std::uint32_t>(reserved);

  std::pmr::vector<std::size_t> begin(graph.blocks.size(), scratch), end(graph.blocks.size(), scratch);
  std::pmr::vector<std::size_t> points(graph.operations.size(), scratch);
  std::pmr::vector<std::pmr::vector<Point>> uses(graph.values.size(), scratch);
  std::size_t position = 1;
  for (const auto& block : graph.blocks) {
    if (!block.id)
      continue;
    begin[block.id.index] = position++;
    for (auto op : block.operations) {
      points[op.index] = position;
      position += 2;
    }
    end[block.id.index] = position++;
  }
  const auto use = [&](SsaValueId id, Point point, SsaEdgeId edge = {}) {
    uses[id.index].push_back(point);
    slots.needed[id.index] = true;
    const auto& value = graph.values[id.index];
    if (value.kind == SsaValueKind::Exception && edge != value.edge)
      slots.capture_exception[id.index] = true;
  };
  const auto transfer_point = [&](const SsaEdge& edge) {
    if (!edge.source)
      return Point{edge.target, begin[edge.target.index]};
    return Point{edge.source, edge.kind == SsaEdgeKind::Exceptional ? points[edge.site.index] : end[edge.source.index]};
  };
  for (const auto& op : graph.operations) {
    if (!op.id)
      continue;
    if (auto charged = budget.spend(1 + op.inputs.size()); !charged)
      return std::unexpected(std::move(charged.error()));
    for (auto id : op.inputs)
      use(id, {op.block, points[op.id.index]});
    if (mirror_locals && op.local_assignment)
      use(op.local_assignment->value, {op.block, points[op.id.index] + 1});
  }
  for (const auto& value : graph.values) {
    if (!value.id || value.kind != SsaValueKind::Phi)
      continue;
    if (auto charged = budget.spend(1 + value.incoming.size()); !charged)
      return std::unexpected(std::move(charged.error()));
    for (const auto& input : value.incoming) {
      const auto& edge = graph.edges[input.edge.index];
      use(input.value, transfer_point(edge), input.edge);
    }
  }

  // A carried prefix must be produced in argument order. Its live ranges may nest
  // (an inner expression is evaluated above an outer operand) but cannot cross.
  // The prefix maximum checks for a crossing with a previously chosen range in O(log n).
  std::pmr::vector<std::size_t> ends(position + 1, 0, scratch);
  std::pmr::vector<std::size_t> starts(scratch);
  for (const auto& block : graph.blocks) {
    for (const auto id : block.operations) {
      const auto& op = graph.operations[id.index];
      if (!op.anchor) {
        starts.clear();
        for (const auto input : op.inputs) {
          if (auto charged = budget.spend(1); !charged)
            return std::unexpected(std::move(charged.error()));
          const auto& value = graph.values[input.index];
          if (value.kind != SsaValueKind::Operation || value.block != block.id || uses[input.index].size() != 1)
            break;
          const auto start = points[value.operation.index];
          if (!starts.empty() && (starts.back() >= start || (op.opcode == Opcode::InvokeSpecial && op.result)))
            break;
          std::size_t latest = 0;
          for (auto i = start - 1; i > 0; i &= i - 1)
            latest = std::max(latest, ends[i]);
          if (latest > start)
            break;
          slots.carried[input.index] = true;
          starts.push_back(start);
        }
        for (auto i : starts)
          for (; i < ends.size(); i += i & (~i + 1))
            ends[i] = std::max(ends[i], points[id.index]);
      }
    }
  }

  std::pmr::vector<Interval> intervals(scratch);
  std::pmr::vector<Point> pending(scratch);
  std::pmr::vector<std::uint32_t> visited(graph.blocks.size(), SSA_NO_SLOT, scratch);
  for (const auto& value : graph.values) {
    if (!value.id)
      continue;
    if ((value.kind == SsaValueKind::RootStack || value.kind == SsaValueKind::RootLocal) &&
        value.type.kind == VerificationKind::UninitializedThis)
      slots.needed[value.id.index] = true;
    if (!slots.needed[value.id.index] || slots.carried[value.id.index])
      continue;
    if (value.kind == SsaValueKind::Parameter && !mirror_locals) {
      slots.homes[value.id.index] = value.slot;
      continue;
    }
    if (value.kind == SsaValueKind::Exception && !slots.capture_exception[value.id.index])
      continue;
    if (auto charged = budget.spend(1 + uses[value.id.index].size()); !charged)
      return std::unexpected(std::move(charged.error()));
    const bool block_definition = value.kind == SsaValueKind::Operation || value.kind == SsaValueKind::Phi;
    std::size_t definition = begin[value.block.index];
    if (value.kind == SsaValueKind::Operation)
      definition = points[value.operation.index] + 1;
    else if (value.kind == SsaValueKind::Exception)
      definition = transfer_point(graph.edges[value.edge.index]).position;
    Interval interval{definition, definition, value.id.index};
    pending.assign(uses[value.id.index].begin(), uses[value.id.index].end());
    while (!pending.empty()) {
      const auto point = pending.back();
      pending.pop_back();
      interval.first = std::min(interval.first, point.position);
      interval.last = std::max(interval.last, point.position);
      if (block_definition && point.block == value.block)
        continue;
      interval.first = std::min(interval.first, begin[point.block.index]);
      if (visited[point.block.index] == value.id.index)
        continue;
      visited[point.block.index] = value.id.index;
      const auto& block = graph.blocks[point.block.index];
      if (auto charged = budget.spend(1 + block.incoming.size()); !charged)
        return std::unexpected(std::move(charged.error()));
      for (auto edge_id : block.incoming) {
        const auto& edge = graph.edges[edge_id.index];
        if (edge.source && edge.id != value.edge)
          pending.push_back(transfer_point(edge));
      }
    }
    intervals.push_back(interval);
  }
  std::ranges::sort(intervals, [](const Interval& a, const Interval& b) {
    return std::tie(a.first, a.value) < std::tie(b.first, b.value);
  });
  using Active = std::tuple<std::size_t, unsigned, std::uint32_t>;
  using ActiveHeap = std::priority_queue<Active, std::pmr::vector<Active>, std::greater<>>;
  using FreeHeap = std::priority_queue<std::uint32_t, std::pmr::vector<std::uint32_t>, std::greater<>>;
  ActiveHeap active(std::greater<>{}, scratch);
  FreeHeap narrow(std::greater<>{}, scratch), wide(std::greater<>{}, scratch);
  for (const auto& interval : intervals) {
    if (auto charged = budget.spend(1); !charged)
      return std::unexpected(std::move(charged.error()));
    while (!active.empty() && std::get<0>(active.top()) < interval.first) {
      const auto [last, width, slot] = active.top();
      (width == 1 ? narrow : wide).push(slot);
      active.pop();
    }
    const auto width = graph.values[interval.value].type.width();
    auto& free = width == 1 ? narrow : wide;
    auto slot = slots.count;
    if (free.empty()) {
      if (width > limit - slots.count)
        return std::unexpected(SsaError{SsaErrorKind::ResourceLimitExceeded, "SSA values exceed the JVM local limit"});
      slots.count += width;
    } else {
      slot = free.top();
      free.pop();
    }
    slots.homes[interval.value] = slot;
    active.emplace(interval.last, width, slot);
  }
  return slots;
}
} // namespace cafe::analysis
