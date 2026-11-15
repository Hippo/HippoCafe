#include "cafe/analysis/frame_analyzer.hpp"

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

namespace cafe::analysis {
namespace {

using enum VerificationKind;

constexpr std::string_view THROWABLE = "java/lang/Throwable";
constexpr std::uint16_t STACK_MAP_MAJOR = 50;
constexpr std::size_t MAX_JOIN_ATTEMPTS = 64;

[[nodiscard]] AnalysisError type_resolution_error(classpath::ResolutionError resolution,
                                                  std::optional<LabelId> label = {}) {
  auto detail = resolution.detail;
  return {.kind = AnalysisErrorKind::TypeResolutionFailure, .label = label, .detail = std::move(detail), .resolution = std::move(resolution)};
}

using Status = std::expected<void, AnalysisError>;

class WorkBudget {
public:
  WorkBudget(const AnalysisLimits& limits, MethodSite site) :
      remaining(limits.max_work), stop(limits.cancellation), site(site) {
  }

  [[nodiscard]] Status spend(std::size_t amount, InstructionId instruction) {
    if (stop.stop_requested())
      return site.fail(AnalysisErrorKind::Cancelled, "bytecode analysis cancelled", instruction);
    if (amount > remaining)
      return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "bytecode analysis work budget exhausted", instruction);
    remaining -= amount;
    return {};
  }

private:
  std::size_t remaining;
  std::stop_token stop;
  MethodSite site;
};

[[nodiscard]] std::size_t frame_cost(const Frame& frame) {
  return 1 + frame.locals.size() + frame.stack.size();
}

} // namespace

AnalysisContext::AnalysisContext(AnalysisLimits limits) :
    AnalysisContext(classpath::ClassHierarchy(classpath::ClassResolver(classpath::ClassPathBuilder{}.build())), limits) {
}

struct FrameAnalysis::Storage {
  Storage(const MethodCode& code, const AnalysisLimits& limits,
          std::uint64_t owner_revision, std::uint64_t hierarchy_snapshot) :
      budget(limits.upstream_resource, limits.max_memory_bytes), names(&budget), entries(&budget),
      handlers(&budget), scope(detail::BytecodeAccess::scope(code)), revision(code.revision()),
      owner_revision(owner_revision), hierarchy_snapshot(hierarchy_snapshot) {
  }

  detail::BudgetResource budget;
  NameSet names;
  std::pmr::vector<StackMapFrame> entries;
  std::pmr::vector<ExceptionHandler> handlers;
  std::uint32_t scope;
  std::uint64_t revision;
  std::uint64_t owner_revision;
  std::uint64_t hierarchy_snapshot;
  std::uint16_t maximum_stack = 0;
  std::uint16_t maximum_locals = 0;
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

class FrameEngine {
public:
  FrameEngine(const ClassFile& owner, const Method& method, const MethodCode& code,
              std::span<const ExceptionHandler> handlers, const AnalysisLimits& limits,
              const classpath::ClassHierarchy* hierarchy, std::span<const InstructionId> additional) : owner(owner), method(method), code(code), handlers(handlers), limits(limits), hierarchy(hierarchy),
                                                                                                       additional(additional), site(method_site(owner, method)) {
  }

  [[nodiscard]] std::expected<FrameAnalysis, AnalysisError> run() const;

private:
  [[nodiscard]] Status run_attempt(FrameAnalysis::Storage& result, const ControlFlowGraph& cfg,
                                   const classpath::ClassHierarchy* resolved_hierarchy,
                                   WorkBudget& work, JoinSearch& search) const;
  [[nodiscard]] std::expected<VerificationType, AnalysisError> handler_type(
      const ProtectedRange& range, const classpath::ClassHierarchy* resolved_hierarchy) const;
  [[nodiscard]] Status materialize(FrameAnalysis::Storage& result, const Frame& state, InstructionId instruction) const;

  const ClassFile& owner;
  const Method& method;
  const MethodCode& code;
  std::span<const ExceptionHandler> handlers;
  const AnalysisLimits& limits;
  const classpath::ClassHierarchy* hierarchy;
  std::span<const InstructionId> additional;
  MethodSite site;
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

Status FrameEngine::materialize(FrameAnalysis::Storage& result, const Frame& state, InstructionId instruction) const {
  const auto has_uninitialized_this = std::ranges::any_of(
      state.locals, [](VerificationType value) { return value.kind == UninitializedThis; });
  if (has_uninitialized_this != state.constructor_uninitialized)
    return site.fail(AnalysisErrorKind::UnrepresentableFrame,
                     "constructor initialization flag cannot be encoded at this frame", instruction);

  auto& entry = result.entries.emplace_back(StackMapFrame{instruction, Frame{&result.budget}});
  auto locals = std::span<const VerificationType>(state.locals);
  while (!locals.empty() && locals.back().kind == Top)
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

std::expected<FrameAnalysis, AnalysisError> FrameEngine::run() const try {
  const auto local_limit = compute_max_locals(method, owner, code, limits);
  if (!local_limit)
    return std::unexpected(local_limit.error());

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

  auto cfg = build_cfg(code, handlers, &result->budget, limits);
  if (!cfg)
    return std::unexpected(site.error(std::move(cfg.error())));
  if (auto spent = work.spend(handlers.size(), code.at(0)); !spent)
    return std::unexpected(std::move(spent.error()));
  if (!handlers.empty())
    result->handlers.assign(handlers.begin(), handlers.end());
  if (resolved_hierarchy && owner.version().major >= STACK_MAP_MAJOR) {
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

Status FrameEngine::run_attempt(FrameAnalysis::Storage& result, const ControlFlowGraph& cfg,
                                const classpath::ClassHierarchy* resolved_hierarchy,
                                WorkBudget& work, JoinSearch& search) const {
  const auto blocks = cfg.blocks();
  const auto local_limit = result.maximum_locals;
  std::pmr::unsynchronized_pool_resource scratch(&result.budget);
  FrameMerger merger(resolved_hierarchy, search, &scratch);
  InstructionEffects simulator(owner, method, resolved_hierarchy, &scratch);
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

  const auto propagate = [&](BlockId target, const Frame& incoming, InstructionId at, bool replay) -> Status {
    if (auto spent = work.spend(frame_cost(incoming), at); !spent)
      return spent;
    auto& entry = entries[target.value];
    if (replay) {
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

  if (resolved_hierarchy) {
    if (auto checked = merger.check(*entries.front(), initial); !checked)
      return std::unexpected(site.error(std::move(checked.error()), blocks.front().first));
    // Recheck every incoming transition against the converged declared type
    // state, including candidates selected for ambiguous reference joins.
    for (std::uint32_t i = 0; i < blocks.size(); ++i)
      if (entries[i])
        if (auto checked = transfer(BlockId{i}, true); !checked)
          return std::unexpected(std::move(checked.error()));
  }
  if (resolved_hierarchy && owner.version().major >= STACK_MAP_MAJOR) {
    std::pmr::vector<bool> explicit_frame(blocks.size(), false, &scratch);
    for (std::size_t i = 0; i < blocks.size(); ++i)
      explicit_frame[i] = blocks[i].frame_boundary;
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
      if (auto spent = work.spend(frame_cost(*entries[i]), blocks[i].first); !spent)
        return std::unexpected(std::move(spent.error()));
      if (auto emitted = materialize(result, *entries[i], blocks[i].first); !emitted)
        return std::unexpected(std::move(emitted.error()));
    }
  }
  result.maximum_stack = static_cast<std::uint16_t>(maximum_stack);
  return {};
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
