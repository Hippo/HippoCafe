/**
 * @file
 * @brief Owned SSA graph construction, queries, and checked edits.
 * @ingroup implementation
 */
#include "ssa_detail.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_map>
#include "../detail/member_signature.hpp"
#include "../io/class_format.hpp"

namespace cafe::analysis {
namespace {

/**
 * @brief Create a graph-level error.
 * @param kind Failure category.
 * @param detail Owned explanation.
 * @return Error with no graph location.
 */
SsaError fail(SsaErrorKind kind, std::string detail) {
  return {kind, std::move(detail)};
}

/**
 * @brief Allocate a process-wide graph generation without wrapping.
 * @return Fresh generation, or budget exception on exhaustion.
 */
std::uint64_t next_scope() {
  static std::atomic<std::uint64_t> next{1};
  auto scope = next.load(std::memory_order_relaxed);
  while (scope != UINT64_MAX) {
    if (next.compare_exchange_weak(scope, scope + 1, std::memory_order_relaxed))
      return scope;
  }
  throw detail::BudgetExceeded();
}

/**
 * @brief Generate the handle for the next append-only table slot.
 * @tparam Id Scoped handle type.
 * @tparam Table Graph table type.
 * @param table Current slot table.
 * @param scope Owning graph generation.
 * @return Fresh typed handle.
 */
template<typename Id, typename Table>
Id fresh_id(const Table& table, std::uint64_t scope) {
  if (table.size() >= UINT32_MAX)
    throw detail::BudgetExceeded();
  return {static_cast<std::uint32_t>(table.size()), scope};
}

/**
 * @brief Resolve a handle only when a live slot has exactly that identity.
 * @tparam Id Scoped handle type.
 * @tparam Table Graph table type.
 * @param table Slot table.
 * @param id Possibly foreign or retired handle.
 * @return Borrowed record, or nullptr.
 */
template<typename Id, typename Table>
auto lookup(const Table& table, Id id) -> const typename Table::value_type* {
  return id && id.index < table.size() && table[id.index].id == id ? &table[id.index] : nullptr;
}

/// Interrupt a guarded read/preflight when the caller signals cancellation.
struct GraphCancelled {};

/**
 * @brief Charge the full linear traversal before modifying a graph.
 * @param s Graph whose nested arrays will be traversed.
 */
void check_scan(const SsaStorage& s) {
  auto remaining = s.limits.max_work;
  const auto charge = [&](std::size_t count) {
    if (s.limits.cancellation.stop_requested())
      throw GraphCancelled{};
    if (count > remaining)
      throw detail::BudgetExceeded();
    remaining -= count;
  };
  charge(s.blocks.size());
  charge(s.edges.size());
  charge(s.operations.size());
  charge(s.values.size());
  for (const auto& op : s.operations)
    charge(op.inputs.size());
  for (const auto& value : s.values)
    charge(value.incoming.size());
}

/**
 * @brief Translate graph policy failures while preserving upstream allocation exceptions.
 * @tparam Work Callback type.
 * @param storage Graph storage, possibly null after moving.
 * @param work Operation returning an expected result.
 * @return The operation result or a structured failure.
 */
template<typename Work>
auto guarded(const SsaStorage* storage, Work work) -> decltype(work()) {
  if (!storage)
    return std::unexpected(fail(SsaErrorKind::InvalidHandle, "moved-from SSA graph"));
  if (storage->limits.cancellation.stop_requested())
    return std::unexpected(fail(SsaErrorKind::Cancelled, "SSA operation cancelled"));
  try {
    return work();
  } catch (const GraphCancelled&) {
    return std::unexpected(fail(SsaErrorKind::Cancelled, "SSA graph scan cancelled"));
  } catch (const detail::BudgetExceeded&) {
    return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA graph resource limit exceeded"));
  }
}

/**
 * @brief Reserve one append with geometric growth before the first mutation.
 * @tparam Vector Contiguous table type.
 * @param vector Destination table.
 */
template<typename Vector>
void reserve_one(Vector& vector) {
  if (vector.size() == vector.capacity()) {
    if (vector.size() == vector.max_size())
      throw detail::BudgetExceeded();
    const auto additional = std::min(vector.size() / 2 + 1, vector.max_size() - vector.size());
    vector.reserve(vector.size() + additional);
  }
}

/**
 * @brief Stage an operation, result and order slot before publishing any of them.
 * @param s Graph storage.
 * @param block Destination block.
 * @param position Validated index into its operation order.
 * @param opcode Logical JVM opcode.
 * @param instruction Class-relative bytecode operand.
 * @param inputs Operand values in evaluation order.
 * @param result Optional verifier type of the normal result.
 * @return New operation handle or failure.
 */
SsaResult<SsaOperationId> insert(SsaStorage& s, SsaBlockId block, std::size_t position, Opcode opcode, Instruction instruction,
                                 std::span<const SsaValueId> inputs, std::optional<SsaType> result) {
  if (!lookup(s.blocks, block))
    return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown operation block"));
  if (s.live_operations >= s.limits.max_instructions || inputs.size() > s.limits.max_work)
    return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA operation or operand limit exceeded"));
  for (const auto input : inputs)
    if (!lookup(s.values, input))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown operation input"));
  auto& order = s.blocks[block.index].operations;
  const auto id = fresh_id<SsaOperationId>(s.operations, s.scope);
  SsaOperation op{.id = id, .block = block, .opcode = opcode, .instruction = instruction, .inputs = std::pmr::vector<SsaValueId>(&s.budget)};
  op.inputs.assign(inputs.begin(), inputs.end());
  reserve_one(order);
  reserve_one(s.operations);
  if (result) {
    if (opcode == Opcode::New && result->kind == VerificationKind::Uninitialized)
      result->allocation = id;
    op.result = s.add_value(SsaValueKind::Operation, block, *result);
    s.values[op.result.index].operation = id;
  }
  s.operations.push_back(std::move(op));
  ++s.live_operations;
  order.insert(order.begin() + static_cast<std::ptrdiff_t>(position), id);
  return id;
}

/**
 * @brief Publish a transfer and both adjacency entries together.
 * @param s Graph storage.
 * @param edge Unpublished transfer record.
 * @return New edge handle or failure.
 */
SsaResult<SsaEdgeId> insert_edge(SsaStorage& s, SsaEdge edge) {
  if (!lookup(s.blocks, edge.target) || (edge.source && !lookup(s.blocks, edge.source)))
    return std::unexpected(fail(SsaErrorKind::InvalidHandle, "edge endpoint belongs to another graph or was erased"));
  if (s.live_edges >= s.limits.max_edges)
    return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA edge limit exceeded"));
  const auto id = fresh_id<SsaEdgeId>(s.edges, s.scope);
  edge.id = id;
  reserve_one(s.edges);
  reserve_one(s.blocks[edge.target.index].incoming);
  if (edge.source)
    reserve_one(s.blocks[edge.source.index].outgoing);
  s.edges.push_back(edge);
  ++s.live_edges;
  s.blocks[edge.target.index].incoming.push_back(id);
  if (edge.source)
    s.blocks[edge.source.index].outgoing.push_back(id);
  return id;
}

/**
 * @brief Erase arguments belonging to an edge about to leave its target.
 * @param storage Graph storage.
 * @param block Current target block.
 * @param edge Redirected or erased edge.
 */
void remove_arguments(SsaStorage& storage, SsaBlockId block, SsaEdgeId edge) {
  for (auto id : storage.blocks[block.index].phis)
    std::erase_if(storage.values[id.index].incoming, [edge](auto item) { return item.edge == edge; });
}

/**
 * @brief Check for surviving executable, metadata or phi uses of a definition.
 * @param s Graph storage.
 * @param id Definition about to be erased.
 * @return Whether erasing the definition would leave a dangling use.
 */
bool used(const SsaStorage& s, SsaValueId id) {
  for (const auto& op : s.operations) {
    if (!op.id)
      continue;
    if (std::ranges::find(op.inputs, id) != op.inputs.end() || (op.local_assignment && op.local_assignment->value == id && op.result != id))
      return true;
  }
  for (const auto& val : s.values)
    for (const auto& input : val.incoming)
      if (input.value == id && val.id != id)
        return true;
  return false;
}

/**
 * @brief Translate a parsed descriptor parameter to an SSA type.
 * @param type Descriptor type.
 * @return Corresponding verifier type with borrowed name.
 */
SsaType descriptor_ssa_type(detail::DescriptorType type) {
  using enum detail::DescriptorKind;
  switch (type.kind) {
    case Integer:
      return {VerificationKind::Integer};
    case Float:
      return {VerificationKind::Float};
    case Long:
      return {VerificationKind::Long};
    case Double:
      return {VerificationKind::Double};
    case Reference:
      return {VerificationKind::Object, type.reference};
    case Void:
      return {};
  }
  return {};
}

/// Deep-copy even cyclic bootstrap arguments without recursing through caller-provided graphs.
class SymbolCopier {
public:
  /**
   * @brief Initialize a graph-copy worklist.
   * @param backing Stable destination storage.
   * @param limits Work and cancellation policy.
   * @param resource Temporary allocator.
   */
  SymbolCopier(SsaSymbols& backing, const AnalysisLimits& limits, std::pmr::memory_resource* resource) : backing(backing), limits(limits), memo(resource), pending(resource) {
  }

  /**
   * @brief Clone a symbolic instruction and all reachable bootstrap arrays.
   * @param input Borrowed source instruction.
   * @return Deep-copied operand or an unsupported operand error.
   */
  SsaResult<MethodInstruction> copy(const MethodInstruction& input) {
    MethodInstruction result;
    if (const auto* f = std::get_if<FieldAccess>(&input)) {
      result = FieldAccess{f->opcode, name(f->owner), name(f->name), name(f->descriptor)};
    } else if (const auto* m = std::get_if<MethodInvocation>(&input)) {
      result = MethodInvocation{m->opcode, name(m->owner), name(m->name), name(m->descriptor), m->owner_is_interface};
    } else if (const auto* d = std::get_if<DynamicInvocation>(&input)) {
      result = DynamicInvocation{name(d->name), name(d->descriptor), bootstrap(d->bootstrap)};
    } else if (const auto* d = std::get_if<DynamicConstant>(&input)) {
      result = dynamic(*d);
    } else {
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "expected symbolic field, method or dynamic operand"));
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      if (limits.cancellation.stop_requested())
        return std::unexpected(fail(SsaErrorKind::Cancelled, "symbolic bootstrap copy cancelled"));
      const auto [src, dst] = pending[i];
      for (std::size_t j = 0; j < src.size(); ++j)
        (*dst)[j].value = std::visit([&](const auto& value) -> BootstrapArgument::Value {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::string_view>)
            return name(value);
          else if constexpr (std::is_same_v<T, MethodTypeConstant>)
            return MethodTypeConstant{name(value.descriptor)};
          else if constexpr (std::is_same_v<T, ClassConstant>)
            return ClassConstant{name(value.name)};
          else if constexpr (std::is_same_v<T, MethodHandle>)
            return handle(value);
          else if constexpr (std::is_same_v<T, DynamicConstant>)
            return dynamic(value);
          else
            return value;
        },
                                     src[j].value);
    }
    return result;
  }

private:
  /**
   * @brief Retain a possibly short string in stable node storage.
   * @param text Caller-owned bytes.
   * @return Stable graph-owned view.
   */
  std::string_view name(std::string_view text) {
    if (text.empty())
      return {};
    return backing.names.emplace_back(text);
  }
  /**
   * @brief Retain a method-handle constant.
   * @param h Source handle.
   * @return Handle whose text belongs to the destination.
   */
  MethodHandle handle(MethodHandle h) {
    return {h.kind, name(h.owner), name(h.name), name(h.descriptor), h.owner_is_interface};
  }
  /**
   * @brief Retain a dynamically computed constant.
   * @param d Source constant.
   * @return Owned symbolic view.
   */
  DynamicConstant dynamic(const DynamicConstant& d) {
    return {name(d.name), name(d.descriptor), bootstrap(d.bootstrap)};
  }
  /**
   * @brief Reserve a stable destination array before enqueueing its arguments.
   * @param b Source bootstrap handle and arguments.
   * @return Bootstrap backed by graph-owned memory.
   */
  Bootstrap bootstrap(const Bootstrap& b) {
    Bootstrap result{handle(b.handle)};
    if (b.arguments.empty())
      return result;
    const auto [begin, end] = memo.equal_range(b.arguments.data());
    for (auto it = begin; it != end; ++it) {
      if (it->second->size() == b.arguments.size()) {
        result.arguments = *it->second;
        return result;
      }
    }
    if (b.arguments.size() > limits.max_work - work)
      throw detail::BudgetExceeded();
    work += b.arguments.size();
    auto& args = backing.arguments.emplace_back();
    args.resize(b.arguments.size());
    memo.emplace(b.arguments.data(), &args);
    pending.emplace_back(b.arguments, &args);
    result.arguments = args;
    return result;
  }
  SsaSymbols& backing; ///< Destination operand backing.
  const AnalysisLimits& limits; ///< Copy work and cancellation policy.
  using Arguments = std::pmr::vector<BootstrapArgument>; ///< Allocator-owned copied argument array.
  std::pmr::unordered_multimap<const BootstrapArgument*, Arguments*> memo; ///< Materialized ranges indexed by source array address.
  std::pmr::vector<std::pair<std::span<const BootstrapArgument>, Arguments*>> pending; ///< Source arrays awaiting element copying.
  std::size_t work = 0; ///< Total reachable arguments charged to the copy.
};

} // namespace

SsaStorage::SsaStorage(std::size_t index, const AnalysisLimits& policy, detail::SsaSourceStamp stamp) : limits(policy), budget(policy.upstream_resource, policy.max_memory_bytes), scope(next_scope()), method_index(index),
                                                                                                        source_stamp(stamp), names(&budget), blocks(&budget), operations(&budget), values(&budget), edges(&budget),
                                                                                                        handlers(&budget), parameters(&budget), symbols(&budget) {
  budget.charge(sizeof(SsaStorage));
}

std::string_view SsaStorage::own(std::string_view name) {
  if (name.empty())
    return {};
  const auto found = names.find(name);
  return found == names.end() ? std::string_view(*names.emplace(name).first) : std::string_view(*found);
}
SsaType SsaStorage::own(SsaType type) {
  type.reference = own(type.reference);
  return type;
}
SsaValueId SsaStorage::add_value(SsaValueKind kind, SsaBlockId block, SsaType type) {
  const auto id = fresh_id<SsaValueId>(values, scope);
  values.push_back({.id = id, .type = own(type), .kind = kind, .block = block, .incoming = std::pmr::vector<SsaPhiInput>(&budget)});
  return id;
}

SsaMethod::SsaMethod(std::unique_ptr<SsaStorage> s) noexcept : storage(std::move(s)) {
}
SsaMethod::~SsaMethod() = default;
SsaMethod::SsaMethod(SsaMethod&&) noexcept = default;
SsaMethod& SsaMethod::operator=(SsaMethod&&) noexcept = default;
std::size_t SsaMethod::method_index() const noexcept {
  return storage ? storage->method_index : SIZE_MAX;
}
SsaBlockId SsaMethod::entry_block() const noexcept {
  return storage && !storage->blocks.empty() ? storage->blocks.front().id : SsaBlockId{};
}
SsaEdgeId SsaMethod::entry_edge() const noexcept {
  return storage && !storage->edges.empty() ? storage->edges.front().id : SsaEdgeId{};
}
std::span<const SsaBlock> SsaMethod::blocks() const noexcept {
  return storage ? std::span<const SsaBlock>(storage->blocks) : std::span<const SsaBlock>{};
}
std::span<const SsaHandler> SsaMethod::handlers() const noexcept {
  return storage ? std::span<const SsaHandler>(storage->handlers) : std::span<const SsaHandler>{};
}
std::span<const SsaValueId> SsaMethod::parameters() const noexcept {
  return storage ? std::span<const SsaValueId>(storage->parameters) : std::span<const SsaValueId>{};
}
const SsaBlock* SsaMethod::block(SsaBlockId id) const noexcept {
  return storage ? lookup(storage->blocks, id) : nullptr;
}
const SsaValue* SsaMethod::value(SsaValueId id) const noexcept {
  return storage ? lookup(storage->values, id) : nullptr;
}
const SsaOperation* SsaMethod::operation(SsaOperationId id) const noexcept {
  return storage ? lookup(storage->operations, id) : nullptr;
}
const SsaEdge* SsaMethod::edge(SsaEdgeId id) const noexcept {
  return storage ? lookup(storage->edges, id) : nullptr;
}
bool SsaMethod::is_current(const ClassFile& owner, const AnalysisContext& context) const noexcept {
  return storage && storage->source_stamp == detail::SsaCommit::source_stamp(owner, storage->method_index, context);
}

SsaResult<SsaMethod> create_ssa(const ClassFile& owner, std::size_t index, AnalysisContext& context) try {
  if (context.limits().cancellation.stop_requested())
    return std::unexpected(fail(SsaErrorKind::Cancelled, "SSA creation cancelled"));
  if (index >= owner.methods().size())
    return std::unexpected(fail(SsaErrorKind::InvalidHandle, "method index out of range"));
  if (owner.version().major < 45 || owner.version().major > 69) {
    AnalysisError cause{.kind = AnalysisErrorKind::UnsupportedClassVersion, .detail = "unsupported SSA class-file version"};
    return std::unexpected(SsaError{.kind = SsaErrorKind::AnalysisFailure, .detail = cause.detail, .analysis = std::move(cause)});
  }
  const auto& method = owner.methods()[index];
  const auto& pool = owner.constant_pool();
  const auto name = pool.get_utf8(method.name_index());
  const auto descriptor = pool.get_utf8(method.descriptor_index());
  const auto self = detail::class_name(pool, owner.this_class());
  if (!name || !descriptor || !self)
    return std::unexpected(fail(SsaErrorKind::InvalidGraph, "invalid method or class identity"));
  if (const char* issue = detail::method_signature_issue(owner.version(), owner.access_flags(), method.access_flags(), *name, *descriptor))
    return std::unexpected(fail(SsaErrorKind::InvalidGraph, issue));
  if (method.opaque_code())
    return std::unexpected(fail(SsaErrorKind::UnsupportedOpaqueCode, "SSA requires a decoded instruction stream for an existing body"));
  if (!detail::needs_code(method.access_flags(), *name))
    return std::unexpected(fail(SsaErrorKind::InvalidGraph, "SSA requires a concrete method declaration"));
  auto graph = SsaAccess::make(std::make_unique<SsaStorage>(index, context.limits(), detail::SsaCommit::source_stamp(owner, index, context)));
  auto& s = SsaAccess::get(graph);
  const auto block = graph.add_block();
  if (!block)
    return std::unexpected(block.error());
  const auto entry = insert_edge(s, {.kind = SsaEdgeKind::Entry, .target = *block});
  if (!entry)
    return std::unexpected(entry.error());
  unsigned slot = 0;
  const auto parameter = [&](SsaType type) {
    if (slot + type.width() > s.limits.max_local_slots)
      throw detail::BudgetExceeded();
    const auto id = s.add_value(SsaValueKind::Parameter, *block, type);
    s.values[id.index].edge = *entry;
    s.values[id.index].slot = slot;
    s.parameters.push_back(id);
    slot += type.width();
  };
  if (!detail::is_static_method(method.access_flags(), *name))
    parameter(*name == "<init>" && *self != "java/lang/Object" ? SsaType{VerificationKind::UninitializedThis} : SsaType{VerificationKind::Object, *self});
  detail::parse_method_descriptor(*descriptor, [&](auto type) { parameter(descriptor_ssa_type(type)); });
  return graph;
} catch (const detail::BudgetExceeded&) {
  return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA graph resource limit exceeded"));
}

SsaResult<SsaBlockId> SsaMethod::add_block() {
  return guarded(storage.get(), [&]() -> SsaResult<SsaBlockId> {
    auto& s = *storage;
    if (s.blocks.size() >= s.limits.max_instructions)
      return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA block limit exceeded"));
    const auto id = fresh_id<SsaBlockId>(s.blocks, s.scope);
    s.blocks.push_back({id, std::pmr::vector<SsaValueId>(&s.budget), std::pmr::vector<SsaOperationId>(&s.budget), std::pmr::vector<SsaEdgeId>(&s.budget), std::pmr::vector<SsaEdgeId>(&s.budget)});
    return id;
  });
}
SsaResult<SsaValueId> SsaMethod::add_phi(SsaBlockId block_id, SsaType type) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaValueId> {
    if (!block(block_id))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown phi block"));
    auto& order = storage->blocks[block_id.index].phis;
    reserve_one(order);
    const auto id = storage->add_value(SsaValueKind::Phi, block_id, type);
    order.push_back(id);
    return id;
  });
}
SsaResult<void> SsaMethod::set_phi_input(SsaValueId phi, SsaEdgeId incoming, SsaValueId input) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* def = value(phi);
    const auto* transfer = edge(incoming);
    if (!def || !value(input) || !transfer)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "invalid phi, edge or value"));
    if (def->kind != SsaValueKind::Phi || transfer->target != def->block)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "phi input is not an incoming edge of its block"));
    auto& arguments = storage->values[phi.index].incoming;
    const auto found = std::ranges::find(arguments, incoming, &SsaPhiInput::edge);
    if (found == arguments.end())
      arguments.push_back({incoming, input});
    else
      found->value = input;
    return {};
  });
}
SsaResult<SsaEdgeId> SsaMethod::add_edge(SsaBlockId source, SsaBlockId target, SsaEdgeKind kind, std::optional<std::int32_t> key) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaEdgeId> {
    if (!block(source))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown source block"));
    if (kind < SsaEdgeKind::Fallthrough || kind > SsaEdgeKind::SwitchDefault ||
        ((kind == SsaEdgeKind::SwitchCase) != key.has_value()))
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "expected normal edge and a case key exactly for switch cases"));
    return insert_edge(*storage, {.kind = kind, .source = source, .target = target, .case_key = key});
  });
}
SsaResult<std::uint32_t> SsaMethod::add_handler(SsaBlockId target, CpIndex catch_type) {
  return guarded(storage.get(), [&]() -> SsaResult<std::uint32_t> {
    if (!block(target))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown catch target"));
    auto& h = storage->handlers;
    if (h.size() >= std::min<std::size_t>(UINT16_MAX, storage->limits.max_handlers))
      return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "SSA handler limit exceeded"));
    const auto ordinal = static_cast<std::uint32_t>(h.size());
    h.push_back({target, catch_type});
    return ordinal;
  });
}
SsaResult<SsaEdgeId> SsaMethod::add_exception_edge(SsaOperationId site, std::uint32_t handler, SsaType caught) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaEdgeId> {
    auto& s = *storage;
    const auto* op = operation(site);
    if (!op || handler >= s.handlers.size())
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "invalid protected operation or exception handler"));
    if (caught.kind != VerificationKind::Object || caught.reference.empty())
      return std::unexpected(fail(SsaErrorKind::TypeMismatch, "caught exception requires a reference class"));
    const auto type = s.own(caught);
    const auto target = s.handlers[handler].target;
    reserve_one(s.values);
    const auto id = fresh_id<SsaValueId>(s.values, s.scope);
    auto edge_id = insert_edge(s, {.kind = SsaEdgeKind::Exceptional, .source = op->block, .target = target, .site = site, .handler = handler, .exception = id});
    if (!edge_id)
      return std::unexpected(edge_id.error());
    s.values.push_back({.id = id, .type = type, .kind = SsaValueKind::Exception, .block = target, .edge = *edge_id, .incoming = std::pmr::vector<SsaPhiInput>(&s.budget)});
    return *edge_id;
  });
}
SsaResult<SsaEdgeId> SsaMethod::add_dead_root(SsaBlockId target, bool constructor_uninitialized) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaEdgeId> {
    if (!block(target))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown disconnected root"));
    for (const auto id : storage->blocks[target.index].incoming) {
      if (!storage->edges[id.index].source)
        return std::unexpected(fail(SsaErrorKind::InvalidGraph, "block already has an entry transfer"));
    }
    return insert_edge(*storage, {.kind = SsaEdgeKind::DeadRoot, .target = target, .constructor_uninitialized = constructor_uninitialized});
  });
}
SsaResult<SsaValueId> SsaMethod::add_root_input(SsaEdgeId root, SsaValueKind kind, std::uint32_t slot, SsaType type) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaValueId> {
    const auto* e = edge(root);
    if (!e)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown dead root"));
    if (e->kind != SsaEdgeKind::DeadRoot || (kind != SsaValueKind::RootStack && kind != SsaValueKind::RootLocal) ||
        type.kind == VerificationKind::Top || type.kind == VerificationKind::WideContinuation)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "root input must be a usable value from a disconnected entry"));
    const auto limit = kind == SsaValueKind::RootLocal ? storage->limits.max_local_slots : storage->limits.max_stack_slots;
    if (slot >= std::min<std::size_t>(limit, UINT16_MAX) ||
        (kind == SsaValueKind::RootLocal && type.width() > std::min<std::size_t>(limit, UINT16_MAX) - slot))
      return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "root slot exceeds JVM or analysis limit"));
    const auto id = storage->add_value(kind, e->target, type);
    auto& v = storage->values[id.index];
    v.edge = root;
    v.slot = slot;
    return id;
  });
}
SsaResult<SsaOperationId> SsaMethod::append_operation(SsaBlockId block_id, Instruction instr,
                                                      std::span<const SsaValueId> inputs, std::optional<SsaType> result) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaOperationId> {
    const auto* b = block(block_id);
    if (!b)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown operation block"));
    return insert(*storage, block_id, b->operations.size(), instr.opcode(), instr, inputs, result);
  });
}
SsaResult<SsaOperationId> SsaMethod::append_symbolic(SsaBlockId block_id, const MethodInstruction& instruction,
                                                     std::span<const SsaValueId> inputs, std::optional<SsaType> result) {
  if (const auto* raw = std::get_if<Instruction>(&instruction))
    return append_operation(block_id, *raw, inputs, result);
  return guarded(storage.get(), [&]() -> SsaResult<SsaOperationId> {
    const auto* b = block(block_id);
    if (!b)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown symbolic operation block"));
    std::pmr::list<SsaSymbols> pending(&storage->budget);
    auto& symbols = pending.emplace_back(&storage->budget);
    SymbolCopier copier(symbols, storage->limits, &storage->budget);
    auto copy = copier.copy(instruction);
    if (!copy)
      return std::unexpected(std::move(copy.error()));
    const auto opcode = std::visit([](const auto& item) {
      using T = std::decay_t<decltype(item)>;
      if constexpr (std::is_same_v<T, MethodInvocation> || std::is_same_v<T, FieldAccess>)
        return item.opcode;
      if constexpr (std::is_same_v<T, DynamicInvocation>)
        return Opcode::InvokeDynamic;
      if constexpr (std::is_same_v<T, DynamicConstant>)
        return item.descriptor == "J" || item.descriptor == "D" ? Opcode::Ldc2W : Opcode::Ldc;
      return Opcode::Nop;
    },
                                   *copy);
    auto id = insert(*storage, block_id, b->operations.size(), opcode, {}, inputs, result);
    if (!id)
      return id;
    symbols.owner = *id;
    storage->symbols.splice(storage->symbols.end(), pending);
    storage->operations[id->index].symbolic = *copy;
    return id;
  });
}
SsaResult<SsaOperationId> SsaMethod::append_terminator(SsaBlockId block_id, Opcode opcode, std::span<const SsaValueId> inputs) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaOperationId> {
    const auto* b = block(block_id);
    if (!b)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown terminator block"));
    const auto& info = opcode_info(opcode);
    if (info.logical_opcode != opcode || info.control_flow == ControlFlow::Next ||
        info.control_flow == ControlFlow::SubroutineCall || info.control_flow == ControlFlow::SubroutineReturn)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "expected a logical JVM terminator"));
    return insert(*storage, block_id, b->operations.size(), opcode, Instruction::simple(opcode).value_or(Instruction{}), inputs, {});
  });
}
SsaResult<SsaOperationId> SsaMethod::insert_before(SsaOperationId at, Instruction instruction, std::span<const SsaValueId> inputs, std::optional<SsaType> result) {
  return guarded(storage.get(), [&]() -> SsaResult<SsaOperationId> {
    const auto* op = operation(at);
    if (!op)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown insertion point"));
    const auto& order = storage->blocks[op->block.index].operations;
    const auto position = std::ranges::find(order, at);
    return insert(*storage, op->block, static_cast<std::size_t>(position - order.begin()), instruction.opcode(), instruction, inputs, result);
  });
}
SsaResult<std::vector<SsaValueId>> SsaMethod::root_inputs(SsaEdgeId root) const {
  return guarded(storage.get(), [&]() -> SsaResult<std::vector<SsaValueId>> {
    const auto* entry = edge(root);
    if (!entry)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown verifier root"));
    if (entry->kind != SsaEdgeKind::Entry && entry->kind != SsaEdgeKind::DeadRoot)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "edge has no verifier root inputs"));
    if (storage->values.size() > storage->limits.max_work)
      return std::unexpected(fail(SsaErrorKind::ResourceLimitExceeded, "root input query exceeds graph scan limit"));
    std::vector<SsaValueId> inputs;
    for (const auto& value : storage->values)
      if (value.id && value.edge == root)
        inputs.push_back(value.id);
    return inputs;
  });
}

SsaResult<SsaOperationId> SsaMethod::insert_symbolic_before(SsaOperationId at, const MethodInstruction& instruction,
                                                            std::span<const SsaValueId> inputs, std::optional<SsaType> result) {
  const auto* anchor = operation(at);
  if (!anchor)
    return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown insertion point"));
  const auto block_id = anchor->block;
  auto& order = storage->blocks[block_id.index].operations;
  const auto index = std::ranges::find(order, at) - order.begin();
  auto inserted = append_symbolic(block_id, instruction, inputs, result);
  if (inserted)
    std::rotate(order.begin() + index, order.end() - 1, order.end());
  return inserted;
}

SsaResult<std::vector<SsaUse>> SsaMethod::uses(SsaValueId id) const {
  return guarded(storage.get(), [&]() -> SsaResult<std::vector<SsaUse>> {
    if (!value(id))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown value"));
    check_scan(*storage);
    std::vector<SsaUse> uses;
    const auto add = [&](SsaUse use) {
      if (uses.size() >= storage->limits.max_memory_bytes / sizeof(SsaUse))
        throw detail::BudgetExceeded();
      uses.push_back(use);
    };
    for (const auto& op : storage->operations) {
      if (!op.id)
        continue;
      for (std::size_t i = 0; i < op.inputs.size(); ++i)
        if (op.inputs[i] == id)
          add({.operation = op.id, .index = i});
      if (op.local_assignment && op.local_assignment->value == id)
        add({.operation = op.id, .local_assignment = true});
    }
    for (const auto& v : storage->values)
      for (std::size_t i = 0; i < v.incoming.size(); ++i)
        if (v.incoming[i].value == id)
          add({.phi = v.id, .edge = v.incoming[i].edge, .index = i});
    return uses;
  });
}
SsaResult<void> SsaMethod::replace_uses(SsaValueId from, SsaValueId to) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    if (!value(from) || !value(to))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown replacement value"));
    if (from == to)
      return {};
    check_scan(*storage);
    for (auto& op : storage->operations) {
      std::ranges::replace(op.inputs, from, to);
      if (op.local_assignment && op.local_assignment->value == from)
        op.local_assignment->value = to;
    }
    for (auto& v : storage->values)
      for (auto& arg : v.incoming)
        if (arg.value == from)
          arg.value = to;
    return {};
  });
}
SsaResult<void> SsaMethod::set_input(SsaOperationId id, std::size_t index, SsaValueId input) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* op = operation(id);
    if (!op || !value(input) || index >= op->inputs.size())
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown operation, operand or replacement value"));
    storage->operations[id.index].inputs[index] = input;
    return {};
  });
}
SsaResult<void> SsaMethod::redirect_edge(SsaEdgeId id, SsaBlockId target) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* e = edge(id);
    if (!e || !block(target))
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown edge or target"));
    if (e->target == target)
      return {};
    if (!e->source || e->kind == SsaEdgeKind::Exceptional)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "only normal edges can be redirected independently"));
    auto& incoming = storage->blocks[target.index].incoming;
    reserve_one(incoming);
    check_scan(*storage);
    remove_arguments(*storage, e->target, id);
    std::erase(storage->blocks[e->target.index].incoming, id);
    incoming.push_back(id);
    storage->edges[id.index].target = target;
    return {};
  });
}
SsaResult<void> SsaMethod::erase_edge(SsaEdgeId id) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* e = edge(id);
    if (!e)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown edge"));
    if (e->kind == SsaEdgeKind::Entry)
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "cannot erase method entry"));
    check_scan(*storage);
    const auto from_edge = [&](SsaValueId input) {
      const auto* definition = value(input);
      return definition && definition->edge == id;
    };
    for (const auto& op : storage->operations)
      if (std::ranges::any_of(op.inputs, from_edge) ||
          (op.local_assignment && from_edge(op.local_assignment->value)))
        return std::unexpected(fail(SsaErrorKind::InvalidGraph, "edge-defined value is used by an operation"));
    for (const auto& v : storage->values)
      for (const auto& input : v.incoming)
        if (input.edge != id && from_edge(input.value))
          return std::unexpected(fail(SsaErrorKind::InvalidGraph, "edge-defined value is used on another transfer"));
    remove_arguments(*storage, e->target, id);
    for (auto& v : storage->values)
      if (v.edge == id)
        v.id = {};
    if (e->source)
      std::erase(storage->blocks[e->source.index].outgoing, id);
    std::erase(storage->blocks[e->target.index].incoming, id);
    storage->edges[id.index].id = {};
    --storage->live_edges;
    return {};
  });
}
SsaResult<void> SsaMethod::erase_operation(SsaOperationId id) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* op = operation(id);
    if (!op)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown operation"));
    check_scan(*storage);
    if (op->result && used(*storage, op->result))
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "operation result has surviving uses"));
    for (const auto& e : storage->edges)
      if (e.id && e.site == id)
        return std::unexpected(fail(SsaErrorKind::InvalidGraph, "remove exceptional edges before erasing their transfer site"));
    for (const auto& v : storage->values)
      if (v.id && v.id != op->result && v.type.allocation == id)
        return std::unexpected(fail(SsaErrorKind::InvalidGraph, "allocation is referenced by a live verification type"));
    std::erase(storage->blocks[op->block.index].operations, id);
    if (op->result)
      storage->values[op->result.index].id = {};
    auto& erased = storage->operations[id.index];
    erased.id = {};
    --storage->live_operations;
    std::pmr::vector<SsaValueId>(&storage->budget).swap(erased.inputs);
    if (erased.symbolic) {
      erased.symbolic.reset();
      auto backing = std::ranges::find(storage->symbols, id, &SsaSymbols::owner);
      if (backing != storage->symbols.end())
        storage->symbols.erase(backing);
    }
    erased.local_assignment.reset();
    return {};
  });
}
SsaResult<void> SsaMethod::erase_phi(SsaValueId id) {
  return guarded(storage.get(), [&]() -> SsaResult<void> {
    const auto* v = value(id);
    if (!v)
      return std::unexpected(fail(SsaErrorKind::InvalidHandle, "unknown phi"));
    check_scan(*storage);
    if (v->kind != SsaValueKind::Phi || used(*storage, id))
      return std::unexpected(fail(SsaErrorKind::InvalidGraph, "erase requires an unused phi"));
    std::erase(storage->blocks[v->block.index].phis, id);
    storage->values[id.index].id = {};
    std::pmr::vector<SsaPhiInput>(&storage->budget).swap(storage->values[id.index].incoming);
    return {};
  });
}

} // namespace cafe::analysis
