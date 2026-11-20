/**
 * @file
 * @brief Bounded SSA liveness and deterministic width-aware linear scan allocation.
 * @ingroup implementation
 */
#include "ssa_slots.hpp"

#include <algorithm>
#include <map>
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

/// Values sharing a physical home, with disjoint live segments in block layout order.
struct Group {
  /**
   * @brief Start a group containing one SSA value.
   * @param index Index of the initial member.
   * @param memory Budgeted scratch allocator.
   */
  Group(std::uint32_t index, std::pmr::memory_resource* memory) : parent(index), live(memory) {
  }
  std::uint32_t parent; ///< Representative after coalescing copies.
  std::pmr::map<std::size_t, std::size_t> live; ///< Inclusive live segments by starting point.
  std::uint32_t home = SSA_NO_SLOT; ///< An ABI parameter slot, or a home selected by linear scan.
};

/**
 * @brief Find the group containing a value and shorten the path to its representative.
 * @param groups Groups indexed by graph value.
 * @param index Member whose group is needed.
 * @return Index of the representative.
 */
std::uint32_t representative(std::span<Group> groups, std::uint32_t index) {
  while (index != groups[index].parent) {
    groups[index].parent = groups[groups[index].parent].parent;
    index = groups[index].parent;
  }
  return index;
}

/**
 * @brief Merge a phi and incoming value if all members can share a home.
 * @param groups Groups indexed by graph value.
 * @param phi Index of the phi result.
 * @param source Index of its incoming value.
 * @param budget Work budget for candidate searches.
 * @return Success (including an interfering pair), or a budget or cancellation error.
 */
SsaResult<void> coalesce(std::span<Group> groups, std::uint32_t phi, std::uint32_t source, SsaLowerBudget& budget) {
  auto a = representative(groups, phi);
  auto b = representative(groups, source);
  if (a == b || groups[a].live.empty() || groups[b].live.empty())
    return {};
  if (groups[a].home != SSA_NO_SLOT && groups[b].home != SSA_NO_SLOT)
    return {}; // Distinct parameters must retain their distinct ABI slots.
  if (groups[a].live.size() < groups[b].live.size())
    std::swap(a, b);
  auto& destination = groups[a];
  auto& incoming = groups[b];
  if (auto work = budget.spend(incoming.live.size()); !work)
    return work;
  for (const auto& [first, last] : incoming.live) {
    const auto after = destination.live.upper_bound(last);
    if (after != destination.live.begin() && std::prev(after)->second >= first)
      return {};
  }
  destination.live.merge(incoming.live);
  if (destination.home == SSA_NO_SLOT)
    destination.home = incoming.home;
  incoming.parent = a;
  return {};
}

} // namespace

SsaResult<SsaSlots> assign_ssa_slots(const SsaStorage& graph, std::size_t reserved,
                                     std::span<const std::uint32_t> mirrored_homes,
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
    ++position; // The preceding position represents a synthetic entry, before simultaneous phis.
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
      return Point{edge.target, begin[edge.target.index] - 1};
    return Point{edge.source, edge.kind == SsaEdgeKind::Exceptional ? points[edge.site.index] : end[edge.source.index]};
  };
  std::pmr::vector<bool> mirrored_writes(mirrored_homes.empty() ? 0 : reserved, false, scratch);
  for (const auto& op : graph.operations) {
    if (!op.id)
      continue;
    if (auto charged = budget.spend(1 + op.inputs.size()); !charged)
      return std::unexpected(std::move(charged.error()));
    for (auto id : op.inputs)
      use(id, {op.block, points[op.id.index]});
    if (op.local_assignment && op.local_assignment->slot < mirrored_homes.size()) {
      const auto [slot, value] = *op.local_assignment;
      const auto home = mirrored_homes[slot];
      if (home != SSA_NO_SLOT) {
        use(value, {op.block, points[op.id.index] + 1});
        for (unsigned n = 0; n < graph.values[value.index].type.width(); ++n)
          mirrored_writes[home + n] = true;
      }
    }
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

  std::pmr::vector<Group> groups(scratch);
  groups.reserve(graph.values.size());
  for (std::uint32_t i = 0; i < graph.values.size(); ++i)
    groups.emplace_back(i, scratch);

  // Preserve holes between live blocks. In particular, a loop phi live at its exit can
  // still share its home with the next iteration's value produced in the loop body.
  std::pmr::vector<std::size_t> first(graph.blocks.size(), SIZE_MAX, scratch), last(graph.blocks.size(), 0, scratch);
  std::pmr::vector<std::uint32_t> touched(scratch);
  const auto record = [&](std::uint32_t block, std::size_t from, std::size_t to) {
    if (first[block] == SIZE_MAX)
      touched.push_back(block);
    first[block] = std::min(first[block], from);
    last[block] = std::max(last[block], to);
  };
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
    auto& group = groups[value.id.index];
    if (value.kind == SsaValueKind::Parameter) {
      if (mirrored_homes.empty()) {
        group.home = value.slot;
      } else if (!mirrored_writes[value.slot] && (value.type.width() == 1 || !mirrored_writes[value.slot + 1])) {
        // Retain a read-only ABI home for its full debug lifetime. Leaving the
        // live set empty prevents a later coalesced result from writing here.
        group.home = value.slot;
        continue;
      }
    }
    if (value.kind == SsaValueKind::Exception && !slots.capture_exception[value.id.index])
      continue;
    if (auto charged = budget.spend(1 + uses[value.id.index].size()); !charged)
      return std::unexpected(std::move(charged.error()));
    const bool block_definition = value.kind == SsaValueKind::Operation || value.kind == SsaValueKind::Phi;
    Point definition{value.block, begin[value.block.index]};
    if (value.kind == SsaValueKind::Operation)
      definition.position = points[value.operation.index] + 1;
    else if (!block_definition)
      definition = transfer_point(graph.edges[value.edge.index]);
    record(definition.block.index, definition.position, definition.position);
    pending.assign(uses[value.id.index].begin(), uses[value.id.index].end());
    while (!pending.empty()) {
      const auto point = pending.back();
      pending.pop_back();
      if (block_definition && point.block == value.block) {
        record(point.block.index, definition.position, point.position);
        continue;
      }
      record(point.block.index, std::min(begin[point.block.index], point.position), point.position);
      if (point.position < begin[point.block.index])
        continue; // An input to a synthetic transfer is live before the block's phis.
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
    for (const auto block : touched) {
      group.live.emplace(first[block], last[block]);
      first[block] = SIZE_MAX;
      last[block] = 0;
    }
    touched.clear();
  }

  for (const auto& value : graph.values) {
    if (!value.id || value.kind != SsaValueKind::Phi)
      continue;
    for (const auto input : value.incoming) {
      if (auto merged = coalesce(groups, value.id.index, input.value.index, budget); !merged)
        return std::unexpected(std::move(merged.error()));
    }
  }
  // Allocate distinct groups with the existing conservative width-aware scan. A pinned
  // group already owns its ABI slot in the reserved prefix.
  std::pmr::vector<Interval> intervals(scratch);
  for (const auto& group : groups)
    if (!group.live.empty() && group.home == SSA_NO_SLOT)
      intervals.push_back({group.live.begin()->first, group.live.rbegin()->second, group.parent});
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
    groups[interval.value].home = slot;
    active.emplace(interval.last, width, slot);
  }
  for (const auto& value : graph.values)
    if (value.id)
      slots.homes[value.id.index] = groups[representative(groups, value.id.index)].home;
  return slots;
}
} // namespace cafe::analysis
