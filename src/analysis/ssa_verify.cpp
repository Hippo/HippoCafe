/**
 * @file
 * @brief Verification of SSA control flow, definitions and JVM instruction contracts.
 * @ingroup implementation
 */

#include <cafe/analysis/ssa.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory_resource>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "analysis_detail.hpp"
#include "detail/descriptor.hpp"
#include "detail/member_signature.hpp"
#include "instruction_effects.hpp"
#include "model/budget_resource.hpp"
#include "model/modified_utf8.hpp"
#include "ssa_detail.hpp"

namespace cafe::analysis {
namespace {

using Kind = VerificationKind; ///< Verification category used by the JVM simulator.
using Status = SsaResult<void>; ///< Successful SSA check or located failure.
using Point = std::uint32_t; ///< Dense index in the program-point graph.
constexpr Point ABSENT = std::numeric_limits<Point>::max(); ///< Sentinel for an unassigned point or arc.

/** @brief Coordinates diagnostics at an individual use, definition or transfer. */
struct Location {
  SsaBlockId block{}; ///< Affected block, if available.
  SsaOperationId operation{}; ///< Affected instruction, if available.
  SsaEdgeId edge{}; ///< Affected transfer, if available.
  std::optional<InstructionId> origin{}; ///< Actual source identity, when a graph operation came from bytecode.

  /**
   * @brief Construct a structured failure with graph coordinates.
   * @param kind SSA failure classification.
   * @param detail Explanation of the failed invariant.
   * @return Located SSA failure.
   */
  std::unexpected<SsaError> fail(SsaErrorKind kind, std::string_view detail) const {
    return std::unexpected(SsaError{kind, std::string(detail), block, operation, edge, {}});
  }

  /**
   * @brief Preserve the verifier cause and classify its public SSA error.
   * @param error Owned underlying error.
   * @return Located SSA failure.
   */
  std::unexpected<SsaError> cause(AnalysisError error) const {
    if (operation)
      error.instruction = origin;
    auto kind = SsaErrorKind::AnalysisFailure;
    if (error.kind == AnalysisErrorKind::ResourceLimitExceeded)
      kind = SsaErrorKind::ResourceLimitExceeded;
    else if (error.kind == AnalysisErrorKind::Cancelled)
      kind = SsaErrorKind::Cancelled;
    else if (error.kind == AnalysisErrorKind::TypeMismatch || error.kind == AnalysisErrorKind::UninitializedUse ||
             error.kind == AnalysisErrorKind::InvalidConstructorState || error.kind == AnalysisErrorKind::StackUnderflow)
      kind = SsaErrorKind::TypeMismatch;
    return std::unexpected(SsaError{kind, error.detail, block, operation, edge, std::move(error)});
  }
};

/** @brief Per-call work ceiling shared by all verifier passes. */
struct VerifyWork {
  const AnalysisLimits& limits; ///< Borrowed analysis limits.
  std::size_t remaining = limits.max_work; ///< Work units still available to this call.

  /**
   * @brief Check cancellation and charge work without unsigned wraparound.
   * @param count Number of work units to charge.
   * @return True when the work may proceed.
   */
  bool spend(std::size_t count = 1) {
    if (count > remaining || limits.cancellation.stop_requested())
      return false;
    remaining -= count;
    return true;
  }

  /**
   * @brief Describe the reason the last work check failed.
   * @param where Graph position associated with the check.
   * @return Cancellation or resource exhaustion.
   */
  std::unexpected<SsaError> failure(Location where = {}) const {
    return limits.cancellation.stop_requested()
               ? where.fail(SsaErrorKind::Cancelled, "SSA verification cancelled")
               : where.fail(SsaErrorKind::ResourceLimitExceeded, "SSA verification work limit exceeded");
  }
};

/** @brief Expanded control flow. An operation's pre-state, its result, and each exceptional
 * transfer have independent dominance positions. */
class ProgramPoints {
  /// Dominance, adjacency and allocation transitions at a single program point.
  struct Node {
    Point first_in = ABSENT; ///< First incoming arc.
    Point first_out = ABSENT; ///< First outgoing arc.
    Point idom = ABSENT; ///< Immediate dominator, with the root dominating itself.
    Point post = ABSENT; ///< DFS postorder position.
    Point begin = 0; ///< Inclusive preorder bound in the dominator tree.
    Point end = 0; ///< Exclusive bound of the dominated subtree.
    Point visited = 0; ///< Generation of the last backward traversal visiting this point.
    InstructionId kill{}; ///< Allocation invalidated by initialization or repeated allocation.
    bool kills_this = false; ///< A constructor attempt invalidates uses of the prior receiver.
    bool initializes_this = false; ///< A successful receiver initialization ends the outstanding obligation.
    bool needs_initialization = false; ///< A path from a root still requires receiver initialization.
  };

  /// An arc linked into both its source successor list and destination predecessor list.
  struct Arc {
    Point from; ///< Source of this directed arc.
    Point to; ///< Destination of this directed arc.
    Point next_in; ///< Next arc entering the same destination.
    Point next_out; ///< Next arc leaving the same source.
  };

public:
  /**
   * @brief Create the super-root used for entry and disconnected frames.
   * @param memory Budgeted scratch allocator for this verification.
   * @param work Shared work and cancellation policy.
   */
  ProgramPoints(std::pmr::memory_resource* memory, VerifyWork& work) :
      nodes(memory), arcs(memory), pending(memory), memory(memory), work(work) {
    add_point(); // All verifier roots have this common predecessor.
  }

  /**
   * @brief Append an isolated program point.
   * @return Stable dense point index.
   */
  Point add_point() {
    const auto id = static_cast<Point>(nodes.size());
    nodes.emplace_back();
    return id;
  }

  /**
   * @brief Link two points in both adjacency directions.
   * @param from Source point.
   * @param to Destination point.
   */
  void connect(Point from, Point to) {
    const auto index = static_cast<Point>(arcs.size());
    arcs.push_back({from, to, nodes[to].first_in, nodes[from].first_out});
    nodes[from].first_out = index;
    nodes[to].first_in = index;
  }

  /**
   * @brief Compute immediate dominators and number their subtrees without recursion.
   * @return Success or a work/cancellation error.
   */
  Status compute_dominance() {
    struct Walk {
      Point node, next;
    };
    std::pmr::vector<Walk> stack(memory);
    std::pmr::vector<Point> order(memory);
    nodes[0].visited = 1;
    stack.push_back({0, nodes[0].first_out});
    while (!stack.empty()) {
      if (!work.spend())
        return work.failure();
      auto& top = stack.back();
      if (top.next == ABSENT) {
        nodes[top.node].post = static_cast<Point>(order.size());
        order.push_back(top.node);
        stack.pop_back();
        continue;
      }
      const auto& arc = arcs[top.next];
      top.next = arc.next_out;
      if (nodes[arc.to].visited == 0) {
        nodes[arc.to].visited = 1;
        stack.push_back({arc.to, nodes[arc.to].first_out});
      }
    }

    // In reverse postorder a reachable predecessor is ready on the first pass.
    nodes[0].idom = 0;
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto it = order.rbegin() + 1; it != order.rend(); ++it) {
        const auto id = *it;
        Point common = ABSENT;
        for (auto e = nodes[id].first_in; e != ABSENT; e = arcs[e].next_in) {
          if (!work.spend())
            return work.failure();
          auto p = arcs[e].from;
          if (nodes[p].idom == ABSENT)
            continue;
          if (common == ABSENT)
            common = p;
          while (p != common) {
            if (!work.spend())
              return work.failure();
            if (nodes[p].post < nodes[common].post)
              p = nodes[p].idom;
            else
              common = nodes[common].idom;
          }
        }
        if (nodes[id].idom != common) {
          nodes[id].idom = common;
          changed = true;
        }
      }
    }

    // Number the dominator tree once for constant-time use-site queries.
    std::pmr::vector<Point> child(nodes.size(), ABSENT, memory);
    std::pmr::vector<Point> sibling(nodes.size(), ABSENT, memory);
    for (const auto id : order) {
      if (!id)
        continue;
      const auto parent = nodes[id].idom;
      sibling[id] = std::exchange(child[parent], id);
    }
    Point counter = 0;
    nodes[0].begin = counter++;
    stack.push_back({0, child[0]});
    while (!stack.empty()) {
      if (!work.spend())
        return work.failure();
      auto& top = stack.back();
      if (top.next == ABSENT) {
        nodes[top.node].end = counter;
        stack.pop_back();
      } else {
        const auto next = top.next;
        top.next = sibling[next];
        nodes[next].begin = counter++;
        stack.push_back({next, child[next]});
      }
    }
    return {};
  }

  /**
   * @brief Check reachability from any declared root.
   * @param point Expanded control-flow position.
   * @return True for a point in the dominance tree.
   */
  bool reachable(Point point) const {
    return point != ABSENT && nodes[point].idom != ABSENT;
  }

  /**
   * @brief Test dominance, including a definition at its own point.
   * @param def Point at which the value becomes available.
   * @param use Program point at which the value is consumed.
   * @return True when the definition is available on every incoming path.
   */
  bool dominates(Point def, Point use) const {
    return reachable(def) && reachable(use) && nodes[def].begin <= nodes[use].begin && nodes[use].begin < nodes[def].end;
  }

  /// Return subtree bounds suitable for a read-only snapshot after verification.
  /// @param point Program point or absent sentinel.
  /// @return Preorder interval, or an empty interval for an absent point.
  SsaDominanceInterval interval(Point point) const {
    return reachable(point) ? SsaDominanceInterval{nodes[point].begin, nodes[point].end} : SsaDominanceInterval{};
  }

  /**
   * @brief Record an allocation identity invalidated at this point.
   * @param point Expanded control-flow position.
   * @param type Verification category and allocation identity.
   */
  void kill_at(Point point, VerificationType type) {
    if (type.kind == Kind::UninitializedThis)
      nodes[point].kills_this = true;
    else
      nodes[point].kill = type.allocation;
  }

  /**
   * @brief Record normal completion of a constructor call on this.
   * @param point Expanded control-flow position.
   */
  void initialize_at(Point point) {
    nodes[point].initializes_this = true;
  }

  // Stop backward searches at the dynamic definition, including loop-carried phis.
  /**
   * @brief Check for an invalidating constructor or allocation along any path from definition to use.
   * @param def Point at which the value becomes available.
   * @param use Program point at which the value is consumed.
   * @param type Verification category and allocation identity.
   * @param where Graph position associated with the check.
   * @return Success or the invalidated input and its location.
   */
  Status check_uninitialized_use(Point def, Point use, VerificationType type, Location where) {
    if (++generation == 0) {
      for (auto& node : nodes)
        node.visited = 0;
      generation = 1;
    }
    pending.clear();
    pending.push_back(use);
    while (!pending.empty()) {
      const auto id = pending.back();
      pending.pop_back();
      auto& node = nodes[id];
      if (id == def || node.visited == generation)
        continue;
      node.visited = generation;
      if (type.kind == Kind::UninitializedThis ? node.kills_this : node.kill == type.allocation)
        return where.fail(SsaErrorKind::TypeMismatch, "an initialization or repeated allocation invalidates this SSA input");
      for (auto e = node.first_in; e != ABSENT; e = arcs[e].next_in) {
        if (!work.spend())
          return work.failure(where);
        if (reachable(arcs[e].from))
          pending.push_back(arcs[e].from);
      }
    }
    return {};
  }

  /**
   * @brief Mark paths on which the current receiver still requires initialization.
   * @param root Entry point with an outstanding initialization requirement.
   * @return Success or a work/cancellation error.
   */
  Status propagate_constructor(Point root) {
    pending.clear();
    pending.push_back(root);
    while (!pending.empty()) {
      const auto p = pending.back();
      pending.pop_back();
      auto& node = nodes[p];
      if (node.needs_initialization || node.initializes_this)
        continue;
      node.needs_initialization = true;
      for (auto e = node.first_out; e != ABSENT; e = arcs[e].next_out) {
        if (!work.spend())
          return work.failure();
        pending.push_back(arcs[e].to);
      }
    }
    return {};
  }

  /**
   * @brief Check outstanding receiver initialization at a return.
   * @param point Expanded control-flow position.
   * @return True if an uninitialized path reaches the point.
   */
  bool uninitialized(Point point) const {
    return nodes[point].needs_initialization;
  }

private:
  std::pmr::vector<Node> nodes; ///< Expanded program-point table.
  std::pmr::vector<Arc> arcs; ///< Contiguous directed arcs.
  std::pmr::vector<Point> pending; ///< Iterative traversal stack shared between checks.
  std::pmr::memory_resource* memory; ///< Budgeted temporary allocator.
  VerifyWork& work; ///< Resource and cancellation accounting.
  Point generation = 1; ///< Traversal epoch for allocation invalidation queries.
};

/**
 * @brief Map a graph-scoped allocation to the identity domain of the private verifier.
 * @param id Graph-scoped identity.
 * @return Synthetic instruction identity used only during this call.
 */
InstructionId site_id(SsaOperationId id) {
  return id ? InstructionId{id.index, 1} : InstructionId{};
}

/**
 * @brief Translate an SSA type for the shared JVM verifier.
 * @param type Verification category and allocation identity.
 * @return Verification type using a synthetic allocation identity.
 */
VerificationType verification_type(SsaType type) {
  return {type.kind, type.reference, site_id(type.allocation)};
}

/**
 * @brief Validate a JVM reference name.
 * @param name Internal class name or array descriptor.
 * @return True for a complete class name or array descriptor.
 */
bool reference_type(std::string_view name) {
  return detail::valid_modified_utf8(name) &&
         (name.starts_with('[') ? detail::valid_field_descriptor(name) : detail::valid_class_name(name));
}

/** @brief Private pool for resolving authored symbolic instructions during const graph verification.
 * The semantic checker continues to inspect the actual owner, declaration and hierarchy. */
class SymbolicOperands {
public:
  /**
   * @brief Resolve a symbolic operand in an isolated temporary class and bind the simulator.
   * @param owner Class defining the method.
   * @param method Original method declaration.
   * @param hierarchy Hierarchy with the current class overlay.
   * @param memory Budgeted scratch allocator for this verification.
   * @param locals Local slots required by the method descriptor.
   * @param operand Authored instruction with symbolic operands.
   * @param where Graph position associated with the check.
   * @return A logical instruction naming the temporary pool, or a located validation error.
   */
  SsaResult<Instruction> resolve(const ClassFile& owner, const Method& method, const classpath::ClassHierarchy& hierarchy,
                                 std::pmr::memory_resource* memory, std::uint16_t locals, const MethodInstruction& operand,
                                 Location where) {
    if (!storage) {
      auto scratch = ClassFile::create(owner.version(), "SsaOperands", "java/lang/Object", 0x0021, memory);
      if (!scratch)
        return where.fail(SsaErrorKind::InvalidGraph, "could not create operand resolution pool");
      auto& pool = scratch->constant_pool();
      const auto name = pool.append_utf8("resolve");
      const auto descriptor = pool.append_utf8("()V");
      const auto code = pool.append_utf8("Code");
      if (!name || !descriptor || !code || !scratch->add_method(0x0008, *name, *descriptor) || !scratch->add_code(0, *code))
        return where.fail(SsaErrorKind::InvalidGraph, "could not prepare symbolic operand resolution");
      storage.emplace(std::move(*scratch));
      effects.emplace(owner, method, &hierarchy, memory, &storage->constant_pool(), true);
      Frame initial(memory);
      if (auto prepared = effects->initialize(initial, locals); !prepared)
        return where.cause(std::move(prepared.error()));
    }
    auto editor = storage->edit_method(0);
    if (!editor)
      return where.fail(SsaErrorKind::InvalidGraph, "missing symbolic operand resolver");
    const auto id = editor->append(operand);
    if (!id)
      return where.fail(SsaErrorKind::InvalidGraph, "invalid symbolic member or bootstrap operand");
    const auto instruction = editor->code()->code().get(*id);
    if (!instruction)
      return where.fail(SsaErrorKind::InvalidGraph, "missing resolved operand");
    return *instruction;
  }

  std::optional<ClassFile> storage; ///< Temporary class owning newly resolved operand constants.
  std::optional<InstructionEffects> effects; ///< JVM instruction simulator bound to a single operand pool.
};

/** @brief Structural and JVM semantic checks over the same graph snapshot. */
class GraphVerifier {
public:
  /**
   * @brief Allocate per-graph indexes and bind the actual class and verifier hierarchy.
   * @param owner Class defining the method.
   * @param graph Borrowed SSA graph under verification.
   * @param hierarchy Hierarchy with the current class overlay.
   * @param limits Resource and cancellation policy.
   * @param memory Budgeted scratch allocator for this verification.
   */
  GraphVerifier(const ClassFile& owner, const SsaMethod& graph, const classpath::ClassHierarchy& hierarchy,
                const AnalysisLimits& limits, std::pmr::memory_resource* memory) :
      owner(owner), method(owner.methods()[graph.method_index()]), graph(graph), data(SsaAccess::get(graph)),
      hierarchy(hierarchy), memory(memory), work{limits}, flow(memory, work),
      entry(data.blocks.size(), ABSENT, memory), tail(data.blocks.size(), ABSENT, memory),
      before(data.operations.size(), ABSENT, memory), after(data.operations.size(), ABSENT, memory),
      transfer(data.edges.size(), ABSENT, memory), defs(data.values.size(), ABSENT, memory),
      killed(data.operations.size(), VerificationType{}, memory), effects(owner, method, &hierarchy, memory, nullptr, true),
      initial(memory), operand_frame(memory) {
  }

  /**
   * @brief Validate program structure, JVM operation semantics and reachable value uses.
   * @return Success or the first violation with its graph location.
   */
  Status verify() {
    if (!graph.entry_block() || !graph.entry_edge())
      return Location{}.fail(SsaErrorKind::InvalidGraph, "missing method entry");
    if (data.handlers.size() > work.limits.max_handlers || data.live_edges > work.limits.max_edges ||
        std::max({data.edges.size(), data.operations.size(), data.values.size(), data.blocks.size()}) >= ABSENT / 8)
      return Location{}.fail(SsaErrorKind::ResourceLimitExceeded, "SSA graph exceeds the verifier limits");
    const auto site = method_site(owner, method);
    const auto slots = detail::method_parameter_slots(site.descriptor);
    if (!slots)
      return Location{}.fail(SsaErrorKind::InvalidGraph, "invalid SSA method descriptor");
    const unsigned total = *slots + (!detail::is_static_method(method.access_flags(), site.name) ? 1u : 0u);
    if (total > work.limits.max_local_slots)
      return Location{}.fail(SsaErrorKind::ResourceLimitExceeded, "method parameters exceed the local slot limit");
    locals = static_cast<std::uint16_t>(total);
    if (auto state = effects.initialize(initial, locals); !state)
      return Location{}.cause(std::move(state.error()));
    for (const auto& symbol : data.symbols) {
      for (const auto& name : symbol.names)
        if (!work.spend(name.size()))
          return work.failure({{}, symbol.owner});
      for (const auto& args : symbol.arguments)
        if (!work.spend(args.size() + 1))
          return work.failure({{}, symbol.owner});
    }
    if (auto status = map_blocks(); !status)
      return status;
    if (auto status = map_edges(); !status)
      return status;
    if (auto status = map_values(); !status)
      return status;
    if (auto status = flow.compute_dominance(); !status)
      return status;
    for (const auto& block : data.blocks) {
      if (!block.id)
        continue;
      if (!flow.reachable(entry[block.id.index]))
        return Location{block.id}.fail(SsaErrorKind::InvalidGraph, "disconnected SSA component has no declared root");
      if (auto valid = check_terminator(block); !valid)
        return valid;
    }
    for (const auto& op : data.operations) {
      if (!op.id)
        continue;
      if (auto valid = check_operation(op); !valid)
        return valid;
    }
    for (const auto& edge : data.edges) {
      if (!edge.id)
        continue;
      if (edge.kind == SsaEdgeKind::Exceptional && killed[edge.site.index].kind != Kind::Top)
        flow.kill_at(transfer[edge.id.index], killed[edge.site.index]);
      if ((edge.kind == SsaEdgeKind::Entry && initial.constructor_uninitialized) ||
          (edge.kind == SsaEdgeKind::DeadRoot && edge.constructor_uninitialized))
        if (auto ok = flow.propagate_constructor(transfer[edge.id.index]); !ok)
          return ok;
    }
    return check_uses();
  }

  /// Publish immutable tree intervals using exactly the program points checked by verify().
  /// @return Success or the work or memory error preventing publication.
  Status save_analysis() {
    if (data.analysis)
      return {};
    const auto uses = data.indexed_uses();
    if (!uses)
      return std::unexpected(uses.error());
    if (!work.spend(entry.size()) || !work.spend(defs.size()) || !work.spend(before.size()) || !work.spend(transfer.size()))
      return work.failure();
    auto result = std::make_shared<SsaAnalysisData>(data.scope, **uses, work.limits, &data.budget);
    const auto copy = [&](const auto& points, auto& intervals) {
      intervals.reserve(points.size());
      for (Point point : points)
        intervals.push_back(flow.interval(point));
    };
    copy(entry, result->blocks);
    copy(defs, result->values);
    copy(before, result->operations);
    copy(transfer, result->edges);
    data.analysis = std::move(result);
    return {};
  }

private:
  /**
   * @brief Assign the entry and normal-success points for every operation.
   * @return Success or a structural or resource error.
   */
  Status map_blocks() {
    std::size_t count = 0;
    for (const auto& block : data.blocks) {
      if (!work.spend())
        return work.failure();
      if (!block.id)
        continue;
      const auto where = Location{block.id};
      if (graph.block(block.id) != &block || block.operations.empty())
        return where.fail(SsaErrorKind::InvalidGraph, "every block needs a terminator");
      auto previous = flow.add_point();
      entry[block.id.index] = previous;
      for (const auto id : block.operations) {
        if (!work.spend())
          return work.failure(where);
        const auto* op = graph.operation(id);
        if (!op || op->block != block.id || before[id.index] != ABSENT)
          return where.fail(SsaErrorKind::InvalidHandle, "invalid or multiply linked operation");
        if (++count > work.limits.max_instructions)
          return where.fail(SsaErrorKind::ResourceLimitExceeded, "SSA operation count exceeds the verifier limit");
        before[id.index] = previous;
        const auto next = flow.add_point();
        flow.connect(previous, next);
        after[id.index] = next;
        previous = next;
        if (id != block.operations.back() && opcode_info(op->opcode).control_flow != ControlFlow::Next)
          return Location{block.id, id}.fail(SsaErrorKind::InvalidGraph, "control transfer before the end of a block");
      }
      tail[block.id.index] = previous;
      for (const auto id : block.phis) {
        const auto* phi = graph.value(id);
        if (!phi || phi->kind != SsaValueKind::Phi || phi->block != block.id || defs[id.index] != ABSENT)
          return where.fail(SsaErrorKind::InvalidHandle, "invalid or multiply linked phi");
        defs[id.index] = entry[block.id.index];
      }
    }
    return {};
  }

  /**
   * @brief Validate normal and exceptional transfers and index their definition points.
   * @return Success or a located edge error.
   */
  Status map_edges() {
    std::pmr::vector<unsigned char> in_count(data.edges.size(), 0, memory);
    std::pmr::vector<unsigned char> out_count(data.edges.size(), 0, memory);
    std::pmr::vector<unsigned char> root_count(data.blocks.size(), 0, memory);
    std::pmr::unordered_set<std::uint64_t> exceptional(memory);
    std::pmr::vector<std::string_view> catches(memory);
    constexpr std::string_view THROWABLE = "java/lang/Throwable";
    for (const auto& handler : data.handlers) {
      if (!graph.block(handler.target))
        return Location{}.fail(SsaErrorKind::InvalidHandle, "unknown handler target");
      auto name = handler.catch_type ? detail::class_name(owner.constant_pool(), handler.catch_type) : std::optional{THROWABLE};
      if (!name || !detail::valid_class_name(*name))
        return Location{handler.target}.fail(SsaErrorKind::InvalidGraph, "invalid catch type");
      if (*name != THROWABLE) {
        auto match = hierarchy.is_assignable(*name, THROWABLE);
        if (!match)
          return resolution(Location{handler.target}, std::move(match.error()));
        if (!*match)
          return Location{handler.target}.fail(SsaErrorKind::TypeMismatch, "handler must catch a Throwable subtype");
      }
      catches.push_back(*name);
    }
    for (const auto& block : data.blocks) {
      if (!block.id)
        continue;
      if (!work.spend(block.incoming.size()) || !work.spend(block.outgoing.size()))
        return work.failure({block.id});
      for (const auto id : block.incoming) {
        const auto* e = graph.edge(id);
        if (!e || e->target != block.id || in_count[id.index]++)
          return Location{block.id, {}, id}.fail(SsaErrorKind::InvalidHandle, "invalid incoming edge");
      }
      for (const auto id : block.outgoing) {
        const auto* e = graph.edge(id);
        if (!e || e->source != block.id || out_count[id.index]++)
          return Location{block.id, {}, id}.fail(SsaErrorKind::InvalidHandle, "invalid outgoing edge");
      }
    }
    for (const auto& edge : data.edges) {
      if (!work.spend())
        return work.failure();
      if (!edge.id)
        continue;
      const Location where{edge.source, edge.site, edge.id};
      if (edge.kind < SsaEdgeKind::Entry || edge.kind > SsaEdgeKind::Exceptional)
        return where.fail(SsaErrorKind::InvalidGraph, "unknown SSA transfer kind");
      const auto id = edge.id.index;
      if (graph.edge(edge.id) != &edge || in_count[id] != 1)
        return where.fail(SsaErrorKind::InvalidGraph, "edge has no unique destination");
      Point source = 0;
      const bool root = edge.kind == SsaEdgeKind::Entry || edge.kind == SsaEdgeKind::DeadRoot;
      if (root) {
        if (edge.source || edge.site || root_count[edge.target.index]++ ||
            ((edge.kind == SsaEdgeKind::Entry) != (edge.id == graph.entry_edge())) ||
            (edge.kind == SsaEdgeKind::Entry && edge.target != graph.entry_block()) ||
            (edge.constructor_uninitialized && !initial.constructor_uninitialized))
          return where.fail(SsaErrorKind::InvalidGraph, "invalid or duplicate entry transfer");
      } else {
        if (out_count[id] != 1)
          return where.fail(SsaErrorKind::InvalidGraph, "edge has no unique source");
        source = tail[edge.source.index];
      }
      if (edge.kind == SsaEdgeKind::Exceptional) {
        const auto* op = graph.operation(edge.site);
        const auto* caught = graph.value(edge.exception);
        if (!op || op->block != edge.source || !edge.handler || *edge.handler >= catches.size() ||
            data.handlers[*edge.handler].target != edge.target || !caught ||
            caught->kind != SsaValueKind::Exception || caught->edge != edge.id ||
            caught->type != SsaType{Kind::Object, catches[*edge.handler]})
          return where.fail(SsaErrorKind::InvalidGraph, "invalid exceptional site, handler or caught value");
        const auto key = (static_cast<std::uint64_t>(edge.site.index) << 32) | *edge.handler;
        if (!exceptional.emplace(key).second)
          return where.fail(SsaErrorKind::InvalidGraph, "duplicate handler at the same instruction");
        source = before[op->id.index];
      } else if (edge.exception || edge.handler || (edge.site && edge.site != data.blocks[edge.source.index].operations.back())) {
        return where.fail(SsaErrorKind::InvalidGraph, "ordinary edge has exceptional payload or invalid site");
      }
      if (edge.case_key.has_value() != (edge.kind == SsaEdgeKind::SwitchCase))
        return where.fail(SsaErrorKind::InvalidGraph, "switch case key on an incompatible edge");
      const auto point = flow.add_point();
      transfer[id] = point;
      flow.connect(source, point);
      flow.connect(point, entry[edge.target.index]);
    }
    return {};
  }

  /**
   * @brief Attach hierarchy lookup failure to the corresponding graph use.
   * @param where Graph position associated with the check.
   * @param error Owned underlying error.
   * @return A public SSA error retaining the resolution cause.
   */
  static std::unexpected<SsaError> resolution(Location where, classpath::ResolutionError error) {
    return where.cause({.kind = AnalysisErrorKind::TypeResolutionFailure,
                        .detail = "could not resolve an SSA verification type",
                        .resolution = std::move(error)});
  }

  /**
   * @brief Validate the reference name or allocation identity of a definition.
   * @param v SSA definition whose type is checked.
   * @return Success or a located type error.
   */
  Status check_type(const SsaValue& v) const {
    const auto where = Location{v.block, v.operation, v.edge};
    const auto& t = v.type;
    switch (t.kind) {
      case Kind::Object:
        if (!reference_type(t.reference) || t.allocation)
          return where.fail(SsaErrorKind::TypeMismatch, "invalid SSA reference type");
        break;
      case Kind::Uninitialized: {
        const auto* site = graph.operation(t.allocation);
        const auto* result = site ? graph.value(site->result) : nullptr;
        if (!site || site->opcode != Opcode::New || !result || result->type != t || !detail::valid_class_name(t.reference) ||
            !detail::valid_modified_utf8(t.reference))
          return where.fail(SsaErrorKind::TypeMismatch, "uninitialized SSA type has no matching allocation");
        break;
      }
      case Kind::UninitializedThis:
        if (!initial.constructor_uninitialized)
          return where.fail(SsaErrorKind::TypeMismatch, "uninitialized this outside a constructor");
        [[fallthrough]];
      case Kind::Integer:
      case Kind::Float:
      case Kind::Long:
      case Kind::Double:
      case Kind::Null:
        if (t.allocation || !t.reference.empty())
          return where.fail(SsaErrorKind::TypeMismatch, "unexpected reference or allocation on SSA type");
        break;
      default:
        return where.fail(SsaErrorKind::TypeMismatch, "unusable verifier slot cannot define an SSA value");
    }
    return {};
  }

  /**
   * @brief Bind each live value to a unique origin and check method parameters and verifier-only inputs.
   * @return Success or a missing or inconsistent definition.
   */
  Status map_values() {
    std::size_t next_slot = 0;
    for (auto id : graph.parameters()) {
      const auto* value = graph.value(id);
      if (!value || value->kind != SsaValueKind::Parameter || value->slot != next_slot ||
          next_slot >= initial.locals.size() || verification_type(value->type) != initial.locals[next_slot] ||
          value->edge != graph.entry_edge() || value->block != graph.entry_block())
        return Location{graph.entry_block()}.fail(SsaErrorKind::TypeMismatch, "SSA parameters disagree with the method descriptor");
      defs[id.index] = transfer[graph.entry_edge().index];
      next_slot += value->type.width();
    }
    if (next_slot != initial.locals.size())
      return Location{graph.entry_block()}.fail(SsaErrorKind::InvalidGraph, "missing SSA parameter");

    std::pmr::unordered_set<std::uint64_t> root_slots(memory);
    struct RootStack {
      std::uint32_t entries = 0;
      std::uint32_t wide = 0;
    };
    std::pmr::unordered_map<std::uint32_t, RootStack> root_stacks(memory);
    for (const auto& v : data.values) {
      if (!work.spend())
        return work.failure();
      if (!v.id)
        continue;
      const Location where{v.block, v.operation, v.edge};
      if (auto ok = check_type(v); !ok)
        return ok;
      if (!graph.block(v.block) || graph.value(v.id) != &v)
        return where.fail(SsaErrorKind::InvalidHandle, "invalid SSA definition");
      if (v.kind == SsaValueKind::Operation) {
        const auto* op = graph.operation(v.operation);
        if (!op || op->block != v.block || op->result != v.id)
          return where.fail(SsaErrorKind::InvalidGraph, "operation definition has no unique producer");
        defs[v.id.index] = after[v.operation.index];
      }
      if (v.kind == SsaValueKind::RootLocal || v.kind == SsaValueKind::RootStack || v.kind == SsaValueKind::Exception) {
        const auto* edge = graph.edge(v.edge);
        if (!edge || edge->target != v.block)
          return where.fail(SsaErrorKind::InvalidHandle, "edge definition has no target");
        if (v.kind == SsaValueKind::Exception) {
          if (edge->exception != v.id || edge->kind != SsaEdgeKind::Exceptional)
            return where.fail(SsaErrorKind::InvalidGraph, "invalid caught-object definition");
        } else {
          const bool local = v.kind == SsaValueKind::RootLocal;
          const auto limit = local ? work.limits.max_local_slots : work.limits.max_stack_slots;
          if (edge->kind != SsaEdgeKind::DeadRoot || v.slot >= UINT16_MAX ||
              v.slot + v.type.width() > std::min<std::size_t>(limit, UINT16_MAX))
            return where.fail(SsaErrorKind::InvalidGraph, "invalid verifier root slot");
          if (v.type.kind == Kind::UninitializedThis && !edge->constructor_uninitialized)
            return where.fail(SsaErrorKind::TypeMismatch, "uninitialized root receiver requires a constructor frame");
          const auto key = (static_cast<std::uint64_t>(v.edge.index) << 32) | (static_cast<std::uint64_t>(local) << 16) | v.slot;
          if (!root_slots.insert(key).second || (local && v.type.width() == 2 && !root_slots.insert(key + 1).second))
            return where.fail(SsaErrorKind::InvalidGraph, "overlapping verifier root slots");
          if (!local) {
            auto& shape = root_stacks[v.edge.index];
            shape.entries = std::max(shape.entries, v.slot + 1);
            shape.wide += v.type.width() - 1;
            if (shape.entries + shape.wide > std::min<std::size_t>(limit, UINT16_MAX))
              return where.fail(SsaErrorKind::ResourceLimitExceeded, "verifier root stack exceeds the slot limit");
          }
        }
        defs[v.id.index] = transfer[v.edge.index];
      }
      if (defs[v.id.index] == ABSENT)
        return where.fail(SsaErrorKind::InvalidGraph, "SSA value has no defining program point");
    }
    return {};
  }

  /**
   * @brief Match branch arms, switch keys and fallthroughs to a block terminator.
   * @param block Block with its ordered operations and outgoing edges.
   * @return Success or a located control-flow error.
   */
  Status check_terminator(const SsaBlock& block) {
    const auto& term = data.operations[block.operations.back().index];
    const Location where{block.id, term.id};
    std::array<std::size_t, static_cast<std::size_t>(SsaEdgeKind::Exceptional) + 1> counts{};
    std::pmr::unordered_set<std::int32_t> keys(memory);
    std::int32_t minimum = INT32_MAX;
    std::int32_t maximum = INT32_MIN;
    std::size_t total = 0;
    for (const auto id : block.outgoing) {
      if (!work.spend())
        return work.failure(where);
      const auto& edge = data.edges[id.index];
      if (edge.kind == SsaEdgeKind::Exceptional)
        continue;
      ++total;
      ++counts[static_cast<std::size_t>(edge.kind)];
      if (edge.case_key) {
        const auto key = *edge.case_key;
        if (!keys.insert(key).second)
          return where.fail(SsaErrorKind::InvalidGraph, "duplicate SSA switch case key");
        minimum = std::min(minimum, key);
        maximum = std::max(maximum, key);
      }
    }
    const auto has = [&](SsaEdgeKind kind) { return counts[static_cast<std::size_t>(kind)] == 1; };
    bool okay = false;
    switch (opcode_info(term.opcode).control_flow) {
      case ControlFlow::Jump:
        okay = total == 1 && (has(SsaEdgeKind::Unconditional) || has(SsaEdgeKind::Fallthrough));
        break;
      case ControlFlow::ConditionalBranch:
        okay = total == 2 && has(SsaEdgeKind::Conditional) && has(SsaEdgeKind::Fallthrough);
        break;
      case ControlFlow::Switch:
        okay = has(SsaEdgeKind::SwitchDefault) && keys.size() == total - 1;
        if (okay && term.opcode == Opcode::TableSwitch)
          okay = !keys.empty() && static_cast<std::int64_t>(maximum) - minimum + 1 == static_cast<std::int64_t>(keys.size());
        break;
      case ControlFlow::Return:
      case ControlFlow::Throw:
        okay = total == 0;
        break;
      default:
        break;
    }
    if (!okay)
      return where.fail(SsaErrorKind::InvalidGraph, "terminator successors disagree with its control-flow kind");
    return {};
  }

  /**
   * @brief Check JVM type assignment at a phi or result declaration.
   * @param actual Verification type provided by the definition.
   * @param expected Type accepted at this use or declaration.
   * @param where Graph position associated with the check.
   * @return Success or a located type or resolution error.
   */
  Status assignable(VerificationType actual, VerificationType expected, Location where) const {
    if (actual == expected || (actual.kind == Kind::Null && expected.kind == Kind::Object))
      return {};
    if (actual.kind == Kind::Object && expected.kind == Kind::Object) {
      auto okay = verifier_reference_assignable(hierarchy, actual.reference, expected.reference);
      if (!okay)
        return resolution(where, std::move(okay.error()));
      if (*okay)
        return {};
    }
    return where.fail(SsaErrorKind::TypeMismatch, "SSA value is not assignable to its declared type");
  }

  /**
   * @brief Apply the JVM instruction transfer to the exact ordered input values.
   * @param op Operation with explicit operands and optional result.
   * @return Success or a located instruction error.
   */
  Status check_operation(const SsaOperation& op) {
    const Location where{op.block, op.id, {}, op.origin};
    if (!work.spend(op.inputs.size() + 1))
      return work.failure(where);
    if (graph.operation(op.id) != &op || before[op.id.index] == ABSENT)
      return where.fail(SsaErrorKind::InvalidHandle, "operation is not linked in a block");
    const auto* result = graph.value(op.result);
    if (op.result && (!result || result->kind != SsaValueKind::Operation || result->operation != op.id))
      return where.fail(SsaErrorKind::InvalidHandle, "invalid operation result");
    if (op.anchor) {
      if (op.opcode != Opcode::Nop || !op.inputs.empty() || op.result || op.symbolic)
        return where.fail(SsaErrorKind::InvalidGraph, "source anchor has executable operands");
      return {};
    }
    operand_frame.stack.clear();
    operand_frame.locals.clear();
    operand_frame.stack_slots = 0;
    for (const auto id : op.inputs) {
      const auto* v = graph.value(id);
      if (!v)
        return where.fail(SsaErrorKind::InvalidHandle, "invalid operation input");
      operand_frame.stack.push_back(verification_type(v->type));
      operand_frame.stack_slots += v->type.width();
      if (operand_frame.stack_slots > UINT16_MAX || operand_frame.stack_slots > work.limits.max_stack_slots)
        return where.fail(SsaErrorKind::ResourceLimitExceeded, "SSA operands exceed the stack limit");
    }
    const auto opcode = op.opcode;
    const auto info = opcode_info(opcode);
    const auto v = owner.version();
    if (!info.is_allowed(v.major) || info.logical_opcode != opcode)
      return where.fail(SsaErrorKind::InvalidGraph, "invalid logical JVM opcode");
    if (info.control_flow == ControlFlow::ConditionalBranch || info.control_flow == ControlFlow::Jump ||
        info.control_flow == ControlFlow::Switch) {
      const bool refs = opcode == Opcode::IfACmpEq || opcode == Opcode::IfACmpNe ||
                        opcode == Opcode::IfNull || opcode == Opcode::IfNonNull;
      const std::size_t count = opcode == Opcode::Goto ? 0u : (opcode >= Opcode::IfICmpEq && opcode <= Opcode::IfACmpNe ? 2u : 1u);
      if (op.inputs.size() != count || result || op.symbolic)
        return where.fail(SsaErrorKind::InvalidGraph, "branch arity disagrees with the opcode");
      for (const auto type : operand_frame.stack) {
        const bool is_ref = type.kind == Kind::Null || type.kind == Kind::Object ||
                            type.kind == Kind::Uninitialized || type.kind == Kind::UninitializedThis;
        if (refs ? !is_ref : type.kind != Kind::Integer)
          return where.fail(SsaErrorKind::TypeMismatch, "incompatible branch or switch operand");
      }
      return {};
    }
    if (opcode == Opcode::IInc) {
      if (!op.instruction.increment_value() || op.symbolic || !result || result->type.kind != Kind::Integer ||
          op.inputs.size() != 1 || operand_frame.stack.front().kind != Kind::Integer)
        return where.fail(SsaErrorKind::TypeMismatch, "iinc requires one integer operand and an integer result");
      return {};
    }
    if (info.form == OperandForm::Local || (opcode >= Opcode::Pop && opcode <= Opcode::Swap) ||
        info.control_flow == ControlFlow::SubroutineCall || info.control_flow == ControlFlow::SubroutineReturn)
      return where.fail(SsaErrorKind::InvalidGraph, "stack, local or subroutine instruction is not a scalar SSA operation");

    auto instruction = op.instruction;
    auto* checker = &effects;
    if (op.symbolic) {
      auto prepared = symbolic.resolve(owner, method, hierarchy, memory, locals, *op.symbolic, where);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      if (!symbolic.effects)
        return where.fail(SsaErrorKind::InvalidGraph, "missing symbolic operand simulator");
      checker = &*symbolic.effects;
      instruction = *prepared;
    } else if (info.control_flow == ControlFlow::Return || info.control_flow == ControlFlow::Throw) {
      auto simple = Instruction::simple(opcode);
      if (!simple)
        return where.fail(SsaErrorKind::InvalidGraph, "invalid method exit opcode");
      instruction = *simple;
    }
    if (instruction.opcode() != opcode)
      return where.fail(SsaErrorKind::InvalidGraph, "SSA operation and JVM operand disagree");
    if (auto ok = checker->prepare(site_id(op.id), instruction, locals); !ok)
      return where.cause(std::move(ok.error()));
    // A path-sensitive pass below checks every constructor return and every use of
    // aliases killed by successful or abrupt initialization.
    operand_frame.constructor_uninitialized = initial.constructor_uninitialized && opcode != Opcode::Return;
    InstructionTransfer trace;
    auto initialized = checker->apply(operand_frame, site_id(op.id), instruction, &trace);
    if (!initialized)
      return where.cause(std::move(initialized.error()));
    if (op.inputs.size() != trace.popped || trace.reordered)
      return where.fail(SsaErrorKind::InvalidGraph, "operand count disagrees with JVM instruction");
    if (*initialized) {
      killed[op.id.index] = **initialized;
      flow.kill_at(after[op.id.index], **initialized);
      if ((**initialized).kind == Kind::UninitializedThis)
        flow.initialize_at(after[op.id.index]);
      if (!result)
        return {};
      const auto name = (**initialized).kind == Kind::UninitializedThis ? method_site(owner, method).class_name : (**initialized).reference;
      return assignable({Kind::Object, name}, verification_type(result->type), where);
    }
    if (trace.produced != (result != nullptr))
      return where.fail(SsaErrorKind::InvalidGraph, "result count disagrees with JVM instruction");
    if (!result)
      return {};
    if (operand_frame.stack.size() != 1)
      return where.fail(SsaErrorKind::InvalidGraph, "invalid result shape for SSA operation");
    const auto actual = operand_frame.stack.front();
    if (opcode == Opcode::New)
      flow.kill_at(after[op.id.index], actual);
    return assignable(actual, verification_type(result->type), where);
  }

  /**
   * @brief Require definition dominance and valid allocation state at a use.
   * @param id Graph-scoped identity.
   * @param point Expanded control-flow position.
   * @param where Graph position associated with the check.
   * @return Success or the offending use.
   */
  Status use(SsaValueId id, Point point, Location where) {
    const auto* v = graph.value(id);
    if (!v)
      return where.fail(SsaErrorKind::InvalidHandle, "invalid SSA use");
    const auto def = defs[id.index];
    if (!flow.dominates(def, point))
      return where.fail(SsaErrorKind::InvalidGraph, "SSA definition does not dominate this use");
    if (v->type.kind == Kind::Uninitialized || v->type.kind == Kind::UninitializedThis)
      return flow.check_uninitialized_use(def, point, verification_type(v->type), where);
    return {};
  }

  /**
   * @brief Validate all operation, debug and edge-specific phi uses.
   * @return Success or the first invalid use.
   */
  Status check_uses() {
    std::pmr::vector<SsaValueId> visited(data.edges.size(), {}, memory);
    for (const auto& phi : data.values) {
      if (!phi.id || phi.kind != SsaValueKind::Phi)
        continue;
      if (!work.spend(phi.incoming.size() + 1))
        return work.failure({phi.block});
      if (phi.incoming.size() != data.blocks[phi.block.index].incoming.size())
        return Location{phi.block}.fail(SsaErrorKind::InvalidGraph, "phi requires one input for each incoming edge");
      for (const auto& input : phi.incoming) {
        const Location where{phi.block, {}, input.edge};
        const auto* e = graph.edge(input.edge);
        const auto* v = graph.value(input.value);
        if (!e || e->target != phi.block || visited[input.edge.index] == phi.id || !v)
          return where.fail(SsaErrorKind::InvalidGraph, "phi input has invalid value, edge or duplicate edge");
        visited[input.edge.index] = phi.id;
        if (auto valid = use(input.value, transfer[input.edge.index], where); !valid)
          return valid;
        if (auto valid = assignable(verification_type(v->type), verification_type(phi.type), where); !valid)
          return valid;
      }
    }
    for (const auto& op : data.operations) {
      if (!op.id)
        continue;
      const Location where{op.block, op.id};
      if (!work.spend(op.inputs.size() + 1))
        return work.failure(where);
      for (const auto id : op.inputs)
        if (auto valid = use(id, before[op.id.index], where); !valid)
          return valid;
      if (const auto assignment = op.local_assignment) {
        if (auto valid = use(assignment->value, after[op.id.index], where); !valid)
          return valid;
      }
      if (opcode_info(op.opcode).control_flow == ControlFlow::Return && flow.uninitialized(before[op.id.index]))
        return where.fail(SsaErrorKind::TypeMismatch, "constructor returns before its receiver is initialized");
    }
    return {};
  }

  const ClassFile& owner; ///< Class being verified.
  const Method& method; ///< Declaration of the selected method.
  const SsaMethod& graph; ///< Graph whose handles are checked on lookup.
  const SsaStorage& data; ///< Graph-owned definition and edge tables.
  const classpath::ClassHierarchy& hierarchy; ///< Owner-aware verifier hierarchy.
  std::pmr::memory_resource* memory; ///< Budgeted temporary allocator.
  VerifyWork work; ///< Resource and cancellation accounting.
  ProgramPoints flow; ///< Expanded control flow with instruction-level exceptional transfer.
  std::pmr::vector<Point> entry; ///< Block entry points.
  std::pmr::vector<Point> tail; ///< Block exit points.
  std::pmr::vector<Point> before; ///< Program points before operation execution.
  std::pmr::vector<Point> after; ///< Program points following normal completion.
  std::pmr::vector<Point> transfer; ///< Point representing each incoming edge.
  std::pmr::vector<Point> defs; ///< Unique defining point for each live value.
  std::pmr::vector<VerificationType> killed; ///< Receiver identity invalidated on exceptional constructor transfer.
  InstructionEffects effects; ///< JVM instruction simulator bound to a single operand pool.
  Frame initial; ///< Descriptor-derived entry state.
  Frame operand_frame; ///< Scratch frame for one operation with explicit SSA operands.
  SymbolicOperands symbolic; ///< Isolated resolver used only by symbolic operations.
  std::uint16_t locals = 0; ///< Descriptor local-slot count.
};

/// Run the verifier, optionally retaining query indexes from this revision.
/// @param owner Class declaring the method.
/// @param graph Bound graph to verify.
/// @param context Verification hierarchy and resource policy.
/// @param save_analysis Whether to retain dominance intervals and index value uses.
/// @return Success or the first verification or resource error.
Status verify_graph(const ClassFile& owner, const SsaMethod& graph, AnalysisContext& context, bool save_analysis) try {
  const auto& limits = context.limits();
  if (limits.cancellation.stop_requested())
    return Location{}.fail(SsaErrorKind::Cancelled, "SSA verification cancelled");
  if (graph.method_index() >= owner.methods().size() || !graph.is_current(owner, context))
    return Location{}.fail(SsaErrorKind::StaleSource, "SSA graph no longer matches its source method or hierarchy");
  if (save_analysis) {
    const auto& cached = SsaAccess::get(graph).analysis;
    if (cached && cached->policy == limits)
      return {};
  }
  auto scoped = context.hierarchy().with_class(owner);
  if (!scoped)
    return Location{}.cause({.kind = AnalysisErrorKind::TypeResolutionFailure, .detail = "cannot bind SSA owner to hierarchy", .resolution = std::move(scoped.error())});
  detail::BudgetResource budget(limits.upstream_resource, limits.max_memory_bytes);
  GraphVerifier verifier(owner, graph, *scoped, limits, &budget);
  auto status = verifier.verify();
  if (status && save_analysis)
    return verifier.save_analysis();
  return status;
} catch (const detail::BudgetExceeded&) {
  return Location{}.fail(SsaErrorKind::ResourceLimitExceeded, "SSA verification memory limit exceeded");
}

} // namespace

SsaResult<void> verify_ssa(const ClassFile& owner, const SsaMethod& graph, AnalysisContext& context) {
  return verify_graph(owner, graph, context, false);
}

SsaResult<SsaAnalysis> analyze_ssa(const ClassFile& owner, const SsaMethod& graph, AnalysisContext& context) {
  if (!graph.entry_block())
    return Location{}.fail(SsaErrorKind::InvalidHandle, "moved-from SSA graph");
  const auto& data = SsaAccess::get(graph);
  std::lock_guard lock(data.index_mutex);
  if (auto result = verify_graph(owner, graph, context, true); !result)
    return std::unexpected(std::move(result.error()));
  return SsaAnalysis(data.analysis);
}

} // namespace cafe::analysis
