#include <cafe/classpath/class_hierarchy.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <cafe/access_flags.hpp>
#include <cafe/class_file.hpp>

#include "classpath_detail.hpp"
#include "detail/descriptor.hpp"

namespace cafe::classpath {
namespace {

using Metadata = std::shared_ptr<const ClassInfo>;
using detail::fail;

struct Reference {
  std::string_view component;
  std::size_t dimensions = 0;
  bool primitive = false;
};

std::expected<Reference, ResolutionError> parse_reference(std::string_view name, const ClassPathLimits& limits) {
  const bool array = name.starts_with('[');
  if (name.size() > limits.max_name_bytes && (!array || name.size() - limits.max_name_bytes > 257)) {
    return fail(ResolutionErrorKind::ResourceLimitExceeded, name.substr(0, limits.max_name_bytes), {},
                "reference name exceeds its configured length limit");
  }
  if (!array)
    return Reference{name};
  const auto invalid = [&] {
    return fail(ResolutionErrorKind::InvalidName, name.substr(0, limits.max_name_bytes), {}, "invalid reference array descriptor");
  };
  if (!cafe::detail::valid_field_descriptor(name))
    return invalid();
  const auto dimensions = name.find_first_not_of('[');
  const auto leaf = name.substr(dimensions);
  if (leaf.size() == 1 && std::string_view("BCDFIJSZ").contains(leaf.front()))
    return Reference{leaf, dimensions, true};
  if (leaf.size() > 2 && leaf.starts_with('L') && leaf.ends_with(';'))
    return Reference{leaf.substr(1, leaf.size() - 2), dimensions, false};
  return invalid();
}

bool array_supertype(std::string_view name) {
  return name == detail::OBJECT || name == detail::CLONEABLE || name == detail::SERIALIZABLE;
}

bool is_interface(const ClassInfo& info) {
  return has_flag(info.access_flags, AccessFlag::Interface);
}

struct Ancestor {
  Metadata info;
  std::size_t depth;
};

using Ancestors = std::unordered_map<std::string_view, Ancestor, detail::NameHash, std::equal_to<>>;
using Closure = std::shared_ptr<const Ancestors>;

void wrap_arrays(JoinResult& result, std::size_t dimensions) {
  if (dimensions == 0)
    return;
  const auto wrap = [&](std::string& name) {
    std::string array(dimensions, '[');
    array += 'L';
    array += name;
    array += ';';
    name = std::move(array);
  };
  wrap(result.type);
  for (auto& candidate : result.ambiguous_candidates)
    wrap(candidate);
}

JoinResult join_classes(std::string_view a, const Ancestors& left, const Ancestors& right) {
  std::unordered_set<std::string_view> less_specific;
  for (const auto& [name, value] : left) {
    if (!right.contains(name))
      continue;
    const auto& info = *value.info;
    if (info.super_name)
      less_specific.insert(*info.super_name);
    less_specific.insert(info.interfaces.begin(), info.interfaces.end());
  }
  std::vector<std::string> most_specific;
  for (const auto& [name, _] : left)
    if (right.contains(name) && !less_specific.contains(name))
      most_specific.emplace_back(name);
  if (most_specific.size() == 1)
    return JoinResult{std::move(most_specific.front()), {}};

  // Ambiguous intersections cannot appear in a StackMapTable. The concrete
  // class chain supplies one deterministic upper bound for data-flow checking.
  auto current = left.find(a);
  while (current != left.end()) {
    if (!is_interface(*current->second.info) && right.contains(current->first))
      break;
    current = current->second.info->super_name ? left.find(*current->second.info->super_name) : left.end();
  }
  std::ranges::sort(most_specific);
  return JoinResult{std::string(current == left.end() ? detail::OBJECT : current->first), std::move(most_specific)};
}

struct PairKey {
  std::string source;
  std::string destination;
};

using PairView = std::pair<std::string_view, std::string_view>;

PairView pair_view(const PairKey& pair) {
  return {pair.source, pair.destination};
}

PairView pair_view(PairView pair) {
  return pair;
}

struct PairHash {
  using is_transparent = void;

  template<typename Pair>
  [[nodiscard]] std::size_t operator()(const Pair& key) const noexcept {
    const auto [source, destination] = pair_view(key);
    const auto first = std::hash<std::string_view>{}(source);
    const auto second = std::hash<std::string_view>{}(destination);
    return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
  }
};

struct PairEqual {
  using is_transparent = void;

  template<typename Left, typename Right>
  [[nodiscard]] bool operator()(const Left& a, const Right& b) const noexcept {
    return pair_view(a) == pair_view(b);
  }
};

} // namespace

struct ClassHierarchy::State {
  explicit State(ClassResolver resolver, Metadata overlay = {}, detail::DeclaredMembers overlay_members = {},
                 std::uint64_t declaration_revision = 0, std::weak_ptr<State> base = {}) :
      resolver(std::move(resolver)), overlay(std::move(overlay)), overlay_members(std::move(overlay_members)),
      declaration_revision(declaration_revision), base(std::move(base)) {
  }

  [[nodiscard]] std::expected<Metadata, ResolutionError> resolve(std::string_view name) const {
    if (resolver.limits().cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, name.substr(0, resolver.limits().max_name_bytes), {}, "hierarchy lookup cancelled");
    if (overlay && name == overlay->name)
      return overlay;
    return resolver.resolve(name);
  }

  // Callers hold mutex so closure cache entries and pair answers belong to
  // the same snapshot and simultaneous lookups cannot duplicate cold work.
  [[nodiscard]] std::expected<Closure, ResolutionError> ancestors(std::string_view name);

  inline static std::atomic<std::uint64_t> next_snapshot_id{1};

  ClassResolver resolver;
  const std::uint64_t snapshot_id = next_snapshot_id.fetch_add(1, std::memory_order_relaxed);
  Metadata overlay;
  detail::DeclaredMembers overlay_members;
  std::uint64_t declaration_revision;
  // The base can retain one recent overlay; the reverse link is weak and
  // overlays never retain sibling overlays.
  std::weak_ptr<State> base;
  std::shared_ptr<State> recent_overlay;
  std::mutex mutex;
  std::unordered_map<std::string_view, Closure, detail::NameHash, std::equal_to<>> closures;
  std::unordered_map<PairKey, bool, PairHash, PairEqual> assignability;
  std::size_t cache_bytes = 0;
};

std::expected<Closure, ResolutionError> ClassHierarchy::State::ancestors(std::string_view name) {
  const auto& limits = resolver.limits();
  if (const auto hit = closures.find(name); hit != closures.end())
    return hit->second;

  struct Node {
    Metadata info;
    std::size_t next_parent = 0;
    std::size_t depth = 1;
    bool parent_started = false;
  };
  Ancestors complete;
  std::vector<Node> stack;
  std::unordered_set<std::string_view> active;
  std::size_t edges = 0;
  std::size_t used_bytes = sizeof(Ancestors);
  constexpr std::size_t NODE_COST = sizeof(Ancestor) + sizeof(std::string_view) + sizeof(Node) + 96;

  const auto too_many = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::ResourceLimitExceeded, name.substr(0, limits.max_name_bytes), {}, reason);
  };
  const auto push = [&](Metadata info) -> std::expected<void, ResolutionError> {
    const auto& type = info->name;
    const auto flags = info->access_flags;
    const bool root = type == detail::OBJECT;
    if (stack.size() >= limits.max_hierarchy_depth || stack.size() + complete.size() >= limits.max_hierarchy_nodes)
      return too_many("hierarchy depth or ancestor count exceeds its configured limit");
    const auto info_size = detail::metadata_size(*info);
    if (limits.max_hierarchy_work_bytes < used_bytes || limits.max_hierarchy_work_bytes - used_bytes < NODE_COST ||
        info_size > limits.max_hierarchy_work_bytes - used_bytes - NODE_COST)
      return too_many("hierarchy work exceeds its configured byte limit");
    used_bytes += NODE_COST + info_size;
    if ((info->version.major >= 53 && has_flag(flags, AccessFlag::Module)) ||
        (root && (info->super_name || is_interface(*info) || !info->interfaces.empty())) ||
        (!root && !info->super_name) ||
        (is_interface(*info) && info->super_name != detail::OBJECT) ||
        (has_flag(flags, AccessFlag::Final) && (is_interface(*info) || has_flag(flags, AccessFlag::Abstract))))
      return fail(ResolutionErrorKind::InvalidParent, type, info->source, "invalid superclass or interface declaration");
    active.insert(type);
    stack.push_back(Node{std::move(info)});
    return {};
  };

  auto root = resolve(name);
  if (!root)
    return std::unexpected(std::move(root.error()));
  if (auto added = push(*root); !added)
    return std::unexpected(std::move(added.error()));
  while (!stack.empty()) {
    if (limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, name, (*root)->source, "hierarchy traversal cancelled");
    auto& node = stack.back();
    const auto& info = *node.info;
    const auto parent_count = info.interfaces.size() + (info.super_name ? 1u : 0u);
    if (node.next_parent == parent_count) {
      complete.emplace(info.name, Ancestor{node.info, node.depth});
      active.erase(info.name);
      stack.pop_back();
      continue;
    }

    const bool super = info.super_name && node.next_parent == 0;
    const auto& parent = super ? *info.super_name : info.interfaces[node.next_parent - (info.super_name ? 1u : 0u)];
    if (!node.parent_started) {
      if (edges == limits.max_hierarchy_edges)
        return too_many("hierarchy edges exceed their configured limit");
      ++edges;
      node.parent_started = true;
    }
    if (active.contains(parent)) {
      std::string path;
      const auto start = std::ranges::find_if(stack, [&](const auto& step) { return step.info->name == parent; });
      for (auto entry = start; entry != stack.end(); ++entry) {
        path += entry->info->name;
        path += " -> ";
      }
      path += parent;
      return fail(ResolutionErrorKind::HierarchyCycle, parent, info.source, path);
    }
    if (const auto done = complete.find(parent); done != complete.end()) {
      const auto& ancestor = *done->second.info;
      if (super ? is_interface(ancestor) || has_flag(ancestor.access_flags, AccessFlag::Final) : !is_interface(ancestor))
        return fail(ResolutionErrorKind::InvalidParent, ancestor.name, ancestor.source,
                    super ? "superclass of " + info.name + " must be a non-final class"
                          : "direct superinterface of " + info.name + " must be an interface");
      if (done->second.depth >= limits.max_hierarchy_depth)
        return too_many("hierarchy path exceeds its configured depth");
      node.depth = std::max(node.depth, done->second.depth + 1);
      ++node.next_parent;
      node.parent_started = false;
      continue;
    }

    auto next = resolve(parent);
    if (!next) {
      auto error = std::move(next.error());
      error.detail = "ancestor of " + info.name + ": " + error.detail;
      return std::unexpected(std::move(error));
    }
    if (auto added = push(*next); !added)
      return std::unexpected(std::move(added.error()));
  }

  auto result = std::make_shared<const Ancestors>(std::move(complete));
  if (used_bytes <= limits.max_hierarchy_cache_bytes - cache_bytes) {
    closures.emplace((*root)->name, result);
    cache_bytes += used_bytes;
  }
  return result;
}

ClassHierarchy::ClassHierarchy(ClassResolver resolver) : state(std::make_shared<State>(std::move(resolver))) {
}

std::uint64_t ClassHierarchy::snapshot_id() const noexcept {
  return state->snapshot_id;
}

std::expected<ClassHierarchy, ResolutionError> ClassHierarchy::with_class(const ClassFile& owner) const {
  const auto& limits = state->resolver.limits();
  if (limits.cancellation.stop_requested())
    return fail(ResolutionErrorKind::Cancelled, {}, "hierarchy overlay cancelled");
  const auto revision = owner.declaration_revision();
  if (state->overlay && state->declaration_revision == revision)
    return *this;

  const auto base = state->overlay ? state->base.lock() : state;
  ClassHierarchy result = *this;
  std::unique_lock<std::mutex> guard;
  if (base) {
    guard = std::unique_lock(base->mutex);
    if (base->recent_overlay && base->recent_overlay->declaration_revision == revision) {
      result.state = base->recent_overlay;
      return result;
    }
  }

  auto replacement = detail::copy_class_metadata(owner, limits);
  if (!replacement)
    return std::unexpected(std::move(replacement.error()));
  if (auto compatible = detail::check_class_release(*replacement, state->resolver.target_release()); !compatible)
    return std::unexpected(std::move(compatible.error()));
  auto members = detail::copy_class_members(owner, limits, *replacement);
  if (!members)
    return std::unexpected(std::move(members.error()));
  const auto size = detail::metadata_size(*replacement);
  if (size > limits.max_in_memory_metadata_bytes || members->byte_size > limits.max_in_memory_metadata_bytes - size)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, replacement->name, replacement->source,
                "owner metadata exceeds the configured in-memory byte limit");

  result.state = std::make_shared<State>(
      state->resolver, std::make_shared<const ClassInfo>(std::move(*replacement)), std::move(*members), revision, base);
  if (base)
    base->recent_overlay = result.state;
  return result;
}

std::expected<Metadata, ResolutionError> ClassHierarchy::resolve(std::string_view name) const {
  return state->resolve(name);
}

std::expected<std::optional<std::uint16_t>, ResolutionError> ClassHierarchy::declared_member_flags(
    std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const {
  if (state->overlay && owner == state->overlay->name) {
    if (state->resolver.limits().cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, owner, state->overlay->source, "member lookup cancelled");
    return state->overlay_members.flags(name, descriptor, method);
  }
  return state->resolver.declared_member_flags(owner, name, descriptor, method);
}

std::expected<bool, ResolutionError> ClassHierarchy::is_assignable(
    std::string_view source, std::string_view destination) const {
  const auto& limits = state->resolver.limits();
  if (limits.cancellation.stop_requested())
    return fail(ResolutionErrorKind::Cancelled, source.substr(0, limits.max_name_bytes), {}, "assignability lookup cancelled");
  const auto from = parse_reference(source, limits);
  if (!from)
    return std::unexpected(from.error());
  const auto to = parse_reference(destination, limits);
  if (!to)
    return std::unexpected(to.error());

  std::scoped_lock guard(state->mutex);
  if (const auto hit = state->assignability.find(PairView{source, destination}); hit != state->assignability.end())
    return hit->second;
  Closure left;
  Closure right;
  if (!from->primitive) {
    auto ancestors = state->ancestors(from->component);
    if (!ancestors)
      return std::unexpected(std::move(ancestors.error()));
    left = std::move(*ancestors);
  }
  if (!to->primitive) {
    auto ancestors = state->ancestors(to->component);
    if (!ancestors)
      return std::unexpected(std::move(ancestors.error()));
    right = std::move(*ancestors);
  }

  bool result = false;
  if (from->dimensions == to->dimensions) {
    result = left && right ? left->contains(to->component)
                           : from->primitive && to->primitive && from->component == to->component;
  } else if (from->dimensions > to->dimensions && right && array_supertype(to->component)) {
    const auto& type = *right->at(to->component).info;
    if (to->component != detail::OBJECT && !is_interface(type))
      return fail(ResolutionErrorKind::InvalidParent, type.name, type.source, "array marker type must be an interface");
    result = true;
  }

  const auto available = limits.max_hierarchy_cache_bytes - state->cache_bytes;
  constexpr std::size_t PAIR_COST = sizeof(PairKey) + 64;
  if (state->assignability.size() < limits.max_pair_cache_entries && available >= PAIR_COST &&
      source.size() <= available - PAIR_COST && destination.size() <= available - PAIR_COST - source.size()) {
    state->assignability.emplace(PairKey{std::string(source), std::string(destination)}, result);
    state->cache_bytes += PAIR_COST + source.size() + destination.size();
  }
  return result;
}

std::expected<JoinResult, ResolutionError> ClassHierarchy::join_reference_types(std::string_view a, std::string_view b) const {
  const auto& limits = state->resolver.limits();
  if (limits.cancellation.stop_requested())
    return fail(ResolutionErrorKind::Cancelled, a.substr(0, limits.max_name_bytes), {}, "type join cancelled");
  const auto first = parse_reference(a, limits);
  if (!first)
    return std::unexpected(first.error());
  const auto second = parse_reference(b, limits);
  if (!second)
    return std::unexpected(second.error());

  std::scoped_lock guard(state->mutex);
  Closure left;
  Closure right;
  if (!first->primitive) {
    auto ancestors = state->ancestors(first->component);
    if (!ancestors)
      return std::unexpected(std::move(ancestors.error()));
    left = std::move(*ancestors);
  }
  if (!second->primitive) {
    auto ancestors = state->ancestors(second->component);
    if (!ancestors)
      return std::unexpected(std::move(ancestors.error()));
    right = std::move(*ancestors);
  }

  auto dimensions = std::min(first->dimensions, second->dimensions);
  if (first->dimensions == second->dimensions && left && right) {
    auto joined = join_classes(first->component, *left, *right);
    wrap_arrays(joined, dimensions);
    return joined;
  }
  if (first->primitive && second->primitive && a == b)
    return JoinResult{std::string(a), {}};

  // Strip the common covariant array dimensions. For arrays of incompatible
  // primitive components, retreat to the last dimension before joining.
  const auto equal_dimensions = first->dimensions == second->dimensions;
  const auto& shallow = first->dimensions <= second->dimensions ? left : right;
  JoinResult joined{std::string(detail::OBJECT), {}};
  if (!equal_dimensions && shallow) {
    std::vector<std::string> candidates;
    for (const auto marker : {detail::SERIALIZABLE, detail::CLONEABLE}) {
      if (shallow->contains(marker)) {
        const auto& type = *shallow->at(marker).info;
        if (!is_interface(type))
          return fail(ResolutionErrorKind::InvalidParent, type.name, type.source, "array marker type must be an interface");
        candidates.emplace_back(marker);
      }
    }
    if (candidates.size() == 1) {
      joined.type = std::move(candidates.front());
    } else {
      joined.ambiguous_candidates = std::move(candidates);
    }
  } else {
    --dimensions;
    joined.ambiguous_candidates.emplace_back(detail::SERIALIZABLE);
    joined.ambiguous_candidates.emplace_back(detail::CLONEABLE);
  }
  if (joined.type == detail::OBJECT && !left && !right) {
    auto root = state->ancestors(detail::OBJECT);
    if (!root)
      return std::unexpected(std::move(root.error()));
  }
  wrap_arrays(joined, dimensions);
  return joined;
}

} // namespace cafe::classpath
