/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#include "cafe/analysis/frame_analyzer.hpp"

#include <utility>

#include <algorithm>
#include <memory_resource>

#include "analysis_detail.hpp"
#include "bytecode/code_access.hpp"
#include "cafe/analysis/control_flow_graph.hpp"
#include "cafe/analysis/max_locals.hpp"
#include "cafe/analysis/max_stack.hpp"
#include "detail/descriptor.hpp"
#include "frame_merge.hpp"
#include "instruction_effects.hpp"
#include "model/budget_resource.hpp"
#include "ssa_frames.hpp"

namespace cafe::analysis {
namespace {

using enum VerificationKind;

constexpr std::string_view THROWABLE = "java/lang/Throwable"; ///< java/lang/Throwable verification identity for catch-all handlers and athrow checks.
constexpr std::uint16_t STACK_MAP_MAJOR = 50; ///< First class-file major with StackMapTable metadata.
constexpr std::size_t MAX_JOIN_ATTEMPTS = 64; ///< Hard ceiling on deterministic ambiguous-reference candidate attempts.

/**
 * @brief Attach optional handler location to a hierarchy lookup failure.
 * @param resolution Underlying hierarchy/source failure retained in the analysis error.
 * @param label Optional scoped label associated with a failure.
 * @return An AnalysisError retaining its ResolutionError.
 * @ingroup implementation
 */
[[nodiscard]] AnalysisError type_resolution_error(classpath::ResolutionError resolution,
                                                  std::optional<LabelId> label = {}) {
  auto detail = resolution.detail;
  return {.kind = AnalysisErrorKind::TypeResolutionFailure, .label = label, .detail = std::move(detail), .resolution = std::move(resolution)};
}

/**
 * @brief Successful verification step or a structured AnalysisError.
 */
using Status = std::expected<void, AnalysisError>;

/**
 * @brief Per-method propagation/candidate-search work counter with cooperative cancellation.
 * @ingroup implementation
 */
class WorkBudget {
public:
  /**
   * @brief Initialize per-method propagation/candidate-search work counter with cooperative
   * cancellation.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @param site Borrowed class/method identity for structured analysis diagnostics.
   * @ingroup implementation
   */
  WorkBudget(const AnalysisLimits& limits, MethodSite site) :
      remaining(limits.max_work), stop(limits.cancellation), site(site) {
  }

  /**
   * @brief Charge work before execution and check cancellation.
   * @param amount Number of analysis work units to charge.
   * @param instruction Stable instruction handle belonging to the supplied method.
   * @return Success or a method-local budget/cancellation failure.
   * @ingroup implementation
   */
  [[nodiscard]] Status spend(std::size_t amount, InstructionId instruction) {
    if (stop.stop_requested())
      return site.fail(AnalysisErrorKind::Cancelled, "bytecode analysis cancelled", instruction);
    if (amount > remaining)
      return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "bytecode analysis work budget exhausted", instruction);
    remaining -= amount;
    return {};
  }

private:
  std::size_t remaining; ///< Unspent analysis work units.
  std::stop_token stop; ///< Cooperative cancellation token checked before charged work.
  MethodSite site; ///< Borrowed method identity used to attach error context.
};

/**
 * @brief Charge one propagation step according to local and operand storage visited.
 * @param frame Borrowed frame whose retained storage cost is estimated.
 * @return Work units for processing the frame.
 * @ingroup implementation
 */
[[nodiscard]] std::size_t frame_cost(const Frame& frame) {
  return 1 + frame.locals.size() + frame.stack.size();
}

} // namespace

AnalysisContext::AnalysisContext(AnalysisLimits limits) :
    AnalysisContext(classpath::ClassHierarchy(classpath::ClassResolver(classpath::ClassPathBuilder{}.build())), std::move(limits)) {
}

/**
 * @brief Result-owned memory budget, interned names, frame vectors, and identity stamps. Resource
 * declaration order keeps allocation owners alive until all dependent containers are destroyed.
 * @ingroup implementation
 */
struct FrameAnalysis::Storage {
  /**
   * @brief Initialize result-owned memory budget, interned names, frame vectors, and identity stamps
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @param owner_revision Class declaration stamp captured for result freshness checks.
   * @param hierarchy_snapshot Hierarchy identity captured by the result.
   */
  Storage(const MethodCode& code, const AnalysisLimits& limits,
          std::uint64_t owner_revision, std::uint64_t hierarchy_snapshot) :
      budget(limits.upstream_resource, limits.max_memory_bytes), names(&budget), entries(&budget),
      handlers(&budget), scope(detail::BytecodeAccess::scope(code)), revision(code.revision()),
      owner_revision(owner_revision), hierarchy_snapshot(hierarchy_snapshot) {
  }

  detail::BudgetResource budget; ///< Owning forwarding resource enforcing charged allocation bytes.
  NameSet names; ///< Stable owned reference-name intern set.
  std::pmr::vector<StackMapFrame> entries; ///< Owned stack-map frame entries in instruction order.
  std::pmr::vector<ExceptionHandler> handlers; ///< Ordered exception-table entries using scoped labels.
  std::uint32_t scope; ///< Method identity used to reject foreign labels or switches.
  std::uint64_t revision; ///< Instruction revision captured when this metadata was supplied.
  std::uint64_t owner_revision; ///< Captured class declaration revision for freshness checks.
  std::uint64_t hierarchy_snapshot; ///< Captured hierarchy snapshot identity for freshness checks.
  std::uint16_t maximum_stack = 0; ///< Computed peak operand-stack size in JVM slots.
  std::uint16_t maximum_locals = 0; ///< Computed local-variable count in JVM slots.
};

FrameAnalysis::FrameAnalysis(std::unique_ptr<Storage> storage) noexcept : storage(std::move(storage)) {
}
FrameAnalysis::~FrameAnalysis() = default;
FrameAnalysis::FrameAnalysis(FrameAnalysis&&) noexcept = default;
FrameAnalysis& FrameAnalysis::operator=(FrameAnalysis&&) noexcept = default;

std::uint16_t FrameAnalysis::max_stack() const noexcept {
  return storage ? storage->maximum_stack : 0;
}
std::uint16_t FrameAnalysis::max_locals() const noexcept {
  return storage ? storage->maximum_locals : 0;
}
std::uint64_t FrameAnalysis::code_revision() const noexcept {
  return storage ? storage->revision : 0;
}
std::span<const StackMapFrame> FrameAnalysis::frames() const noexcept {
  return storage ? std::span<const StackMapFrame>(storage->entries) : std::span<const StackMapFrame>{};
}
bool FrameAnalysis::is_current(const MethodCode& code, std::span<const ExceptionHandler> handlers) const noexcept {
  return storage && storage->scope == detail::BytecodeAccess::scope(code) && code_revision() == code.revision() &&
         std::ranges::equal(storage->handlers, handlers);
}

bool FrameAnalysis::is_current(const ClassFile& owner, const Method& method,
                               const AnalysisContext& context) const noexcept {
  if (!storage || storage->owner_revision != owner.declaration_revision() ||
      storage->hierarchy_snapshot != context.hierarchy().snapshot_id() || !owns_method(owner, method))
    return false;
  const auto* attribute = method.code();
  return attribute && is_current(attribute->code(), attribute->exception_handlers());
}

/**
 * @brief Nonmutating method verifier driver. It builds a CFG, validates all instruction effects,
 * propagates frames under shared budgets, retries bounded reference joins, and materializes a
 * self-contained result.
 * @ingroup implementation
 */
class FrameEngine {
public:
  /// Select frame collection and assignment rules for the analysis caller.
  enum class Purpose {
    ComputedEmission, ///< Preserve the existing frame inference and metadata contract.
    SymbolicSimulation, ///< Materialize every typed block entry for the SSA lifter.
    LoweredEmission, ///< Verify the final SSA output and collect only the required wire frames.
  };
  /**
   * @brief Initialize nonmutating method verifier driver
   * @param owner Class owning the method, constants, or source slices being inspected.
   * @param method Method belonging to the supplied class.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param handlers Exception-table entries in class-file order using the supplied code's labels.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @param hierarchy Reference-resolution hierarchy; a null internal pointer selects category-only
   *   analysis.
   * @param additional Additional required frame-entry instruction identities.
   * @param seeds Fixed verifier constraints at instruction boundaries.
   * @param purpose Frame selection and assignment rules for this caller.
   */
  FrameEngine(const ClassFile& owner, const Method& method, const MethodCode& code,
              std::span<const ExceptionHandler> handlers, const AnalysisLimits& limits,
              const classpath::ClassHierarchy* hierarchy, std::span<const InstructionId> additional,
              std::span<const StackMapFrame> seeds = {}, Purpose purpose = Purpose::ComputedEmission) :
      owner(owner), method(method), code(code), handlers(handlers), limits(limits), hierarchy(hierarchy),
      additional(additional), seeds(seeds), all_entries(purpose == Purpose::SymbolicSimulation),
      verifier_interfaces(purpose != Purpose::ComputedEmission), site(method_site(owner, method)) {
  }

  /**
   * @brief Validate analysis inputs and retry bounded reference joins until a safe frame solution is
   * found.
   * @return An owned analysis result or structured verification/resource failure.
   */
  [[nodiscard]] std::expected<FrameAnalysis, AnalysisError> run() const;

private:
  /**
   * @brief Propagate one deterministic join choice through the CFG, recheck converged transitions,
   * and materialize frames.
   * @param result Output/result storage populated by this operation.
   * @param cfg Validated control-flow graph for the analyzed code and handler snapshot.
   * @param resolved_hierarchy Optional hierarchy overlay for the current owner; null selects
   *   category-only analysis.
   * @param work Shared work/cancellation budget across propagation attempts.
   * @param search Deterministic reference-join candidate selection state shared across attempts.
   * @return Success or a failure for this choice; all attempts share work/memory budgets.
   */
  [[nodiscard]] Status run_attempt(FrameAnalysis::Storage& result, const ControlFlowGraph& cfg,
                                   const classpath::ClassHierarchy* resolved_hierarchy,
                                   WorkBudget& work, JoinSearch& search) const;
  /**
   * @brief Resolve and validate a handler catch reference, including catch-all Throwable.
   * @param range Protected exception-table interval being analyzed.
   * @param resolved_hierarchy Optional hierarchy overlay for the current owner; null selects
   *   category-only analysis.
   * @return The reference verification type, or a hierarchy/type error.
   */
  [[nodiscard]] std::expected<VerificationType, AnalysisError> handler_type(
      const ProtectedRange& range, const classpath::ClassHierarchy* resolved_hierarchy) const;
  /**
   * @brief Copy a converged frame into result-owned storage and intern reference names.
   * @param result Output/result storage populated by this operation.
   * @param state Converged frame copied into result-owned storage.
   * @param instruction Stable instruction handle belonging to the supplied method.
   * @param wire_frame Whether constructor state must be encodable at this boundary.
   * @return Success or an unrepresentable-frame error.
   */
  [[nodiscard]] Status materialize(FrameAnalysis::Storage& result, const Frame& state, InstructionId instruction,
                                   bool wire_frame) const;

  /// Check verifier-only inputs before exposing their state to instruction transfer.
  /// @param seed Declaration containing verifier slots and a current instruction identity.
  /// @return Success or an invalid declaration or analysis limit error.
  [[nodiscard]] Status validate_seed(const StackMapFrame& seed) const;

  const ClassFile& owner; ///< Borrowed class owning the analyzed method and constant pool.
  const Method& method; ///< Borrowed owning method declaration.
  const MethodCode& code; ///< Borrowed immutable instruction sequence being analyzed.
  std::span<const ExceptionHandler> handlers; ///< Ordered exception-table entries using scoped labels.
  const AnalysisLimits& limits; ///< Configured resource ceilings.
  const classpath::ClassHierarchy* hierarchy; ///< Optional borrowed reference hierarchy; null selects category-only checking.
  std::span<const InstructionId> additional; ///< Extra instruction identities requiring frames in an emission plan.
  std::span<const StackMapFrame> seeds; ///< Fixed entry constraints, including disconnected roots.
  bool all_entries; ///< Collect symbolically usable frames at all blocks and all supported versions.
  bool verifier_interfaces; ///< Apply JVM interface assignment for SSA and its emitted code.
  MethodSite site; ///< Borrowed method identity used to attach error context.
};

std::expected<VerificationType, AnalysisError> FrameEngine::handler_type(
    const ProtectedRange& range, const classpath::ClassHierarchy* resolved_hierarchy) const {
  const auto& handler = range.handler;
  if (!handler.catch_type)
    return VerificationType{.kind = Object, .reference = THROWABLE};

  const auto name = detail::class_name(owner.constant_pool(), handler.catch_type);
  if (!name) {
    auto error = site.error({.kind = AnalysisErrorKind::InvalidConstantPoolReference,
                             .label = handler.handler,
                             .detail = "catch type must name a non-array class"});
    return std::unexpected(std::move(error));
  }
  if (resolved_hierarchy && *name != THROWABLE) {
    auto assignable = resolved_hierarchy->is_assignable(*name, THROWABLE);
    if (!assignable)
      return std::unexpected(site.error(type_resolution_error(std::move(assignable.error()), handler.handler)));
    if (!*assignable) {
      auto error = site.error({.kind = AnalysisErrorKind::TypeMismatch, .label = handler.handler, .detail = "catch type must be java/lang/Throwable or a subclass"});
      return std::unexpected(std::move(error));
    }
  }
  return VerificationType{.kind = Object, .reference = *name};
}

Status FrameEngine::materialize(FrameAnalysis::Storage& result, const Frame& state, InstructionId instruction,
                                bool wire_frame) const {
  const auto has_uninitialized_this = std::ranges::any_of(
      state.locals, [](VerificationType value) { return value.kind == UninitializedThis; });
  if (wire_frame && has_uninitialized_this != state.constructor_uninitialized)
    return site.fail(AnalysisErrorKind::UnrepresentableFrame,
                     "constructor initialization flag cannot be encoded at this frame", instruction);

  auto& entry = result.entries.emplace_back(StackMapFrame{instruction, Frame{&result.budget}});
  auto locals = std::span<const VerificationType>(state.locals);
  while (!all_entries && !locals.empty() && locals.back().kind == Top)
    locals = locals.first(locals.size() - 1);
  const auto values = [&](auto& destination, std::span<const VerificationType> source) -> Status {
    destination.reserve(source.size());
    for (auto value : source) {
      if (value.kind == Uninitialized) {
        const auto allocated = code.get(value.allocation);
        if (!allocated || allocated->opcode() != Opcode::New)
          return site.fail(AnalysisErrorKind::UnrepresentableFrame,
                           "uninitialized value does not identify a new instruction", instruction);
      }
      if (value.kind == Object &&
          (value.reference.size() > UINT16_MAX ||
           !(value.reference.starts_with('[') ? detail::valid_field_descriptor(value.reference)
                                              : detail::valid_class_name(value.reference))))
        return site.fail(AnalysisErrorKind::UnrepresentableFrame,
                         "object verification type cannot be encoded as a class constant", instruction);
      value.reference = intern_name(result.names, value.reference);
      destination.push_back(value);
    }
    return {};
  };

  if (auto copied = values(entry.frame.locals, locals); !copied)
    return copied;
  if (auto copied = values(entry.frame.stack, state.stack); !copied)
    return copied;
  entry.frame.stack_slots = state.stack_slots;
  entry.frame.constructor_uninitialized = state.constructor_uninitialized;
  return {};
}

Status FrameEngine::validate_seed(const StackMapFrame& seed) const {
  const auto invalid = [&](std::string_view reason) {
    return site.fail(AnalysisErrorKind::UnrepresentableFrame, reason, seed.instruction);
  };
  if (owner.version().major < STACK_MAP_MAJOR)
    return invalid("declared frames require class-file major version 50 or later");
  const auto& frame = seed.frame;
  if (frame.locals.size() > UINT16_MAX || frame.locals.size() > limits.max_local_slots ||
      frame.stack.size() > UINT16_MAX || frame.stack_slots > limits.max_stack_slots)
    return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "declared verifier frame exceeds slot limits", seed.instruction);
  const auto valid_value = [&](VerificationType value) {
    switch (value.kind) {
      case Object:
        return value.reference.starts_with('[') ? detail::valid_field_descriptor(value.reference)
                                                : detail::valid_class_name(value.reference);
      case Uninitialized: {
        const auto allocation = code.get(value.allocation);
        if (!allocation || allocation->opcode() != Opcode::New)
          return false;
        return detail::class_name(owner.constant_pool(), allocation->constant_index().value_or(CpIndex{})) == value.reference;
      }
      case UninitializedThis:
        return site.name == "<init>" && site.class_name != "java/lang/Object";
      case Top:
      case Integer:
      case Float:
      case Long:
      case Double:
      case Null:
      case WideContinuation:
        return true;
    }
    return false;
  };
  bool has_receiver = false;
  for (std::size_t i = 0; i < frame.locals.size(); ++i) {
    const auto value = frame.locals[i];
    if (!valid_value(value) || value.kind == WideContinuation)
      return invalid("invalid declared local verification type");
    has_receiver |= value.kind == UninitializedThis;
    if (value.width() == 2 && (++i == frame.locals.size() || frame.locals[i].kind != WideContinuation))
      return invalid("declared category-two local lacks a continuation");
  }
  if (has_receiver != frame.constructor_uninitialized)
    return invalid("declared frame has an inconsistent constructor state");
  unsigned slots = 0;
  for (const auto value : frame.stack) {
    if (!valid_value(value) || value.kind == WideContinuation)
      return invalid("invalid declared operand verification type");
    slots += value.width();
  }
  if (slots != frame.stack_slots || slots > UINT16_MAX)
    return invalid("declared operand width differs from its stack slot count");
  return {};
}

std::expected<FrameAnalysis, AnalysisError> FrameEngine::run() const try {
  auto local_limit = compute_max_locals(method, owner, code, limits);
  if (!local_limit)
    return std::unexpected(std::move(local_limit.error()));

  std::optional<classpath::ClassHierarchy> scoped;
  if (hierarchy) {
    auto overlay = hierarchy->with_class(owner);
    if (!overlay)
      return std::unexpected(site.error(type_resolution_error(std::move(overlay.error()))));
    scoped.emplace(std::move(*overlay));
  }
  const auto* resolved_hierarchy = scoped ? &*scoped : nullptr;
  WorkBudget work(limits, site);
  if (auto spent = work.spend(code.size(), code.at(0)); !spent)
    return std::unexpected(std::move(spent.error()));
  auto result = std::make_unique<FrameAnalysis::Storage>(
      code, limits, owner.declaration_revision(), hierarchy ? hierarchy->snapshot_id() : 0);
  result->maximum_locals = *local_limit;
  for (const auto& seed : seeds) {
    if (auto spent = work.spend(frame_cost(seed.frame), seed.instruction); !spent)
      return std::unexpected(std::move(spent.error()));
    if (auto valid = validate_seed(seed); !valid)
      return std::unexpected(std::move(valid.error()));
    result->maximum_locals = std::max(result->maximum_locals, static_cast<std::uint16_t>(seed.frame.locals.size()));
  }

  auto cfg = ssa_cfg(code, handlers, seeds, &result->budget, limits);
  if (!cfg)
    return std::unexpected(site.error(std::move(cfg.error())));
  if (auto spent = work.spend(handlers.size(), code.at(0)); !spent)
    return std::unexpected(std::move(spent.error()));
  if (!handlers.empty())
    result->handlers.assign(handlers.begin(), handlers.end());
  if (seeds.empty() && resolved_hierarchy && (owner.version().major >= STACK_MAP_MAJOR || all_entries)) {
    const auto blocks = cfg->blocks();
    const auto dead = std::ranges::find(blocks, false, &BasicBlock::reachable);
    if (dead != blocks.end())
      return site.fail(AnalysisErrorKind::UnreachableCode,
                       "unreachable code has no inferable StackMapTable frame", dead->first);
  }

  JoinSearch search(&result->budget);
  auto first = run_attempt(*result, *cfg, resolved_hierarchy, work, search);
  if (first)
    return FrameAnalysis(std::move(result));
  if (first.error().kind != AnalysisErrorKind::AmbiguousTypeJoin)
    return std::unexpected(std::move(first.error()));

  // The choices belong to individual frame slots. Reuse the graph and
  // verify each combination within the same method work and memory budgets.
  std::size_t attempts = 1;
  for (; attempts < MAX_JOIN_ATTEMPTS && search.advance(); ++attempts) {
    result->entries.clear();
    result->names.clear();
    auto retried = run_attempt(*result, *cfg, resolved_hierarchy, work, search);
    if (retried)
      return FrameAnalysis(std::move(result));
    if (retried.error().kind == AnalysisErrorKind::Cancelled ||
        retried.error().kind == AnalysisErrorKind::ResourceLimitExceeded)
      return std::unexpected(std::move(retried.error()));
  }
  if (attempts == MAX_JOIN_ATTEMPTS)
    first.error().detail += " (reference join search reached its attempt limit)";
  return std::unexpected(std::move(first.error()));
} catch (const detail::BudgetExceeded&) {
  return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "frame analysis memory budget exhausted");
}

// Entry frames are engaged before a block is queued; only initialized reachable/frame-boundary entries are materialized.
Status FrameEngine::run_attempt(FrameAnalysis::Storage& result, const ControlFlowGraph& cfg,
                                const classpath::ClassHierarchy* resolved_hierarchy,
                                WorkBudget& work, JoinSearch& search) const {
  const auto blocks = cfg.blocks();
  const auto local_limit = result.maximum_locals;
  std::pmr::unsynchronized_pool_resource scratch(&result.budget);
  FrameMerger merger(resolved_hierarchy, search, &scratch, verifier_interfaces);
  InstructionEffects simulator(owner, method, resolved_hierarchy, &scratch, nullptr, verifier_interfaces);
  Frame initial(&scratch);
  if (auto initialized = simulator.initialize(initial, local_limit); !initialized)
    return std::unexpected(site.error(std::move(initialized.error())));
  for (const auto& entry : code.instructions()) {
    if (auto spent = work.spend(1, entry.id); !spent)
      return std::unexpected(std::move(spent.error()));
    if (auto prepared = simulator.prepare(entry.id, entry.instruction, local_limit); !prepared)
      return std::unexpected(site.error(std::move(prepared.error()), entry.id));
  }

  std::pmr::vector<VerificationType> catch_types(&scratch);
  catch_types.reserve(handlers.size());
  for (const auto& range : cfg.exception_ranges()) {
    if (auto spent = work.spend(1, blocks[range.target.value].first); !spent)
      return std::unexpected(std::move(spent.error()));
    auto type = handler_type(range, resolved_hierarchy);
    if (!type)
      return std::unexpected(std::move(type.error()));
    catch_types.push_back(*type);
  }

  std::pmr::vector<std::optional<Frame>> entries(blocks.size(), &scratch);
  std::pmr::vector<bool> fixed(blocks.size(), false, &scratch);
  std::pmr::vector<bool> queued(blocks.size(), false, &scratch);
  std::pmr::vector<BlockId> pending(&scratch);
  pending.reserve(blocks.size());
  const auto enqueue = [&](BlockId target) {
    if (!queued[target.value]) {
      queued[target.value] = true;
      pending.push_back(target);
    }
  };
  copy_frame(entries.front().emplace(&scratch), initial);
  enqueue(BlockId{0});
  for (const auto& seed : seeds) {
    const auto id = cfg.block_of(seed.instruction);
    if (!id)
      return site.fail(AnalysisErrorKind::InvalidLabelOrRange, "declared frame has no block", seed.instruction);
    auto& frame = entries[id->value].emplace(&scratch);
    if (auto spent = work.spend(1 + local_limit + seed.frame.stack.size(), seed.instruction); !spent)
      return spent;
    copy_frame(frame, seed.frame);
    frame.locals.resize(local_limit);
    fixed[id->value] = true;
    enqueue(*id);
  }

  const auto propagate = [&](BlockId target, const Frame& incoming, InstructionId at, bool replay) -> Status {
    if (auto spent = work.spend(frame_cost(incoming), at); !spent)
      return spent;
    auto& entry = entries[target.value];
    if (replay || fixed[target.value]) {
      auto checked = merger.check(*entry, incoming);
      if (!checked)
        return std::unexpected(site.error(std::move(checked.error()), at));
      return {};
    }
    if (!entry) {
      copy_frame(entry.emplace(&scratch), incoming);
      enqueue(target);
      return {};
    }
    auto changed = merger.merge(*entry, incoming, blocks[target.value].first);
    if (!changed)
      return std::unexpected(site.error(std::move(changed.error()), blocks[target.value].first));
    if (*changed)
      enqueue(target);
    return {};
  };

  Frame state(&scratch);
  Frame exceptional(&scratch);
  exceptional.stack.resize(1);
  exceptional.stack_slots = 1;
  std::pmr::vector<std::uint32_t> active_handlers(&scratch);
  unsigned maximum_stack = 0;
  const unsigned wire_stack_limit = detail::short_code_header(owner.version()) ? UINT8_MAX : UINT16_MAX;
  const auto check_peak = [&](unsigned slots, InstructionId at) -> Status {
    if (slots > wire_stack_limit)
      return site.fail(AnalysisErrorKind::StackOverflow, "operand stack cannot fit in max_stack", at);
    if (slots > limits.max_stack_slots)
      return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "operand stack exceeds the slot limit", at);
    maximum_stack = std::max(maximum_stack, slots);
    return {};
  };

  const auto transfer = [&](BlockId id, bool replay) -> Status {
    const auto& block = blocks[id.value];
    if (!block.reachable && block.end == code.size()) {
      const auto flow = opcode_info(code.get(block.last)->opcode()).control_flow;
      if (flow == ControlFlow::Next || flow == ControlFlow::ConditionalBranch)
        return site.fail(AnalysisErrorKind::InvalidCode, "frame-seeded code falls off the end of Code", block.last);
    }
    if (auto spent = work.spend(frame_cost(*entries[id.value]), block.first); !spent)
      return spent;
    copy_frame(state, *entries[id.value]);
    if (auto peak = check_peak(state.stack_slots, block.first); !peak)
      return peak;
    cfg.handlers_for(id, active_handlers);

    for (auto position = block.begin; position < block.end; ++position) {
      const auto at = code.at(position);
      const auto instruction = *code.get(at);
      std::size_t cost = 1;
      if (!active_handlers.empty())
        cost += state.locals.size();
      if (instruction.opcode() == Opcode::New || instruction.opcode() == Opcode::InvokeSpecial)
        cost += frame_cost(state);
      if (auto spent = work.spend(cost, at); !spent)
        return spent;

      if (!active_handlers.empty()) {
        exceptional.locals = state.locals;
        exceptional.constructor_uninitialized = state.constructor_uninitialized;
      }
      auto initialized = simulator.apply(state, at, instruction);
      if (!initialized) {
        auto error = std::move(initialized.error());
        if (search.encountered_ambiguity() && error.kind == AnalysisErrorKind::TypeMismatch) {
          error.kind = AnalysisErrorKind::AmbiguousTypeJoin;
          error.detail = "selected common reference type failed an instruction constraint (" + error.detail + ")";
        }
        return std::unexpected(site.error(std::move(error), at));
      }
      if (auto peak = check_peak(state.stack_slots, at); !peak)
        return peak;
      if (active_handlers.empty())
        continue;

      // The exception sees the incoming locals. The JVM additionally forbids
      // reuse of any receiver alias if its initialization terminated abruptly.
      if (*initialized)
        std::ranges::replace(exceptional.locals, **initialized, VerificationType{});
      for (const auto index : active_handlers) {
        exceptional.stack.front() = catch_types[index];
        if (auto sent = propagate(cfg.exception_ranges()[index].target, exceptional, at, replay); !sent)
          return sent;
      }
    }
    for (const auto& edge : cfg.outgoing(id))
      if (auto sent = propagate(edge.target, state, block.last, replay); !sent)
        return sent;
    return {};
  };

  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    queued[id.value] = false;
    if (auto done = transfer(id, false); !done)
      return std::unexpected(std::move(done.error()));
  }

  if (all_entries || !seeds.empty()) {
    for (std::size_t i = 0; i < blocks.size(); ++i)
      if (!entries[i])
        return site.fail(AnalysisErrorKind::UnreachableCode,
                         "unreachable instruction has no declared verifier entry state", blocks[i].first);
  }

  if (resolved_hierarchy) {
    if (auto checked = merger.check(*entries.front(), initial); !checked) // NOLINT(bugprone-unchecked-optional-access)
      return std::unexpected(site.error(std::move(checked.error()), blocks.front().first));
    // Recheck every incoming transition against the converged declared type
    // state, including candidates selected for ambiguous reference joins.
    for (std::uint32_t i = 0; i < blocks.size(); ++i)
      if (entries[i])
        if (auto checked = transfer(BlockId{i}, true); !checked)
          return std::unexpected(std::move(checked.error()));
  }
  if (resolved_hierarchy && (owner.version().major >= STACK_MAP_MAJOR || all_entries)) {
    std::pmr::vector<bool> explicit_frame(blocks.size(), false, &scratch);
    for (std::size_t i = 0; i < blocks.size(); ++i)
      explicit_frame[i] = all_entries || blocks[i].frame_boundary;
    for (const auto instruction : additional) {
      if (auto spent = work.spend(1, instruction); !spent)
        return std::unexpected(std::move(spent.error()));
      const auto id = cfg.block_of(instruction);
      if (!id || blocks[id->value].first != instruction)
        return site.fail(AnalysisErrorKind::InvalidLabelOrRange, "additional frame point is not a block entry", instruction);
      explicit_frame[id->value] = true;
    }
    const auto count = static_cast<std::size_t>(std::ranges::count(explicit_frame, true));
    if (count > limits.max_frames || count > UINT16_MAX)
      return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "StackMapTable frame limit exceeded");
    result.entries.reserve(count);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      if (!explicit_frame[i])
        continue;
      if (auto spent = work.spend(frame_cost(*entries[i]), blocks[i].first); !spent) // NOLINT(bugprone-unchecked-optional-access)
        return std::unexpected(std::move(spent.error()));
      const bool wire_frame = owner.version().major >= STACK_MAP_MAJOR && (!all_entries || blocks[i].frame_boundary);
      if (auto emitted = materialize(result, *entries[i], blocks[i].first, wire_frame); !emitted) // NOLINT(bugprone-unchecked-optional-access)
        return std::unexpected(std::move(emitted.error()));
    }
  }
  result.maximum_stack = static_cast<std::uint16_t>(maximum_stack);
  return {};
}

std::expected<SsaFrames, AnalysisError> ssa_frames(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, AnalysisContext& context, std::span<const StackMapFrame> seeds) {
  auto result = FrameEngine(owner, method, code, handlers, context.limits(), &context.hierarchy(), {}, seeds,
                            FrameEngine::Purpose::SymbolicSimulation)
                    .run();
  if (!result)
    return std::unexpected(std::move(result.error()));
  const auto entries = result->frames();
  const auto locals = result->max_locals();
  const auto stack = result->max_stack();
  return SsaFrames{std::move(*result), entries, locals, stack};
}

std::expected<FrameAnalysis, AnalysisError> emission_frames(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, AnalysisContext& context, std::span<const StackMapFrame> seeds) {
  return FrameEngine(owner, method, code, handlers, context.limits(), &context.hierarchy(), {}, seeds,
                     FrameEngine::Purpose::LoweredEmission)
      .run();
}

std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context) {
  const auto* code = method.code();
  if (!code)
    return method_site(owner, method).fail(AnalysisErrorKind::MissingCode, "frame computation requires decoded Code");
  return compute_frames(owner, method, context, code->code(), code->exception_handlers());
}

std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context,
    const MethodCode& code, std::span<const ExceptionHandler> handlers, std::span<const InstructionId> additional) {
  return FrameEngine(owner, method, code, handlers, context.limits(), &context.hierarchy(), additional).run();
}

std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const AnalysisLimits& limits) {
  const auto* code = method.code();
  if (!code)
    return method_site(owner, method).fail(AnalysisErrorKind::MissingCode, "max_stack requires decoded Code");
  return compute_max_stack(owner, method, code->code(), code->exception_handlers(), limits);
}

std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, const AnalysisLimits& limits) {
  auto analyzed = FrameEngine(owner, method, code, handlers, limits, nullptr, {}).run();
  if (!analyzed)
    return std::unexpected(std::move(analyzed.error()));
  return analyzed->max_stack();
}

} // namespace cafe::analysis
