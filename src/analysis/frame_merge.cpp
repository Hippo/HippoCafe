/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#include "frame_merge.hpp"

namespace cafe::analysis {
namespace {

using enum VerificationKind;

/**
 * @brief Create a frame-shape incompatibility failure.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected IncompatibleJoin error.
 * @ingroup implementation
 */
[[nodiscard]] std::unexpected<AnalysisError> incompatible(std::string detail) {
  return std::unexpected(AnalysisError{.kind = AnalysisErrorKind::IncompatibleJoin, .detail = std::move(detail)});
}

/**
 * @brief Wrap hierarchy failure during frame merging.
 * @param resolution Underlying hierarchy/source failure retained in the analysis error.
 * @return An unexpected TypeResolutionFailure retaining the cause.
 * @ingroup implementation
 */
[[nodiscard]] std::unexpected<AnalysisError> unresolved(classpath::ResolutionError resolution) {
  auto detail = resolution.detail;
  return std::unexpected(AnalysisError{.kind = AnalysisErrorKind::TypeResolutionFailure,
                                       .detail = std::move(detail),
                                       .resolution = std::move(resolution)});
}

/**
 * @brief Compare value counts and category widths before merging operand types.
 * @param a First frame whose operand-stack values and slot count are compared.
 * @param b Second frame whose operand-stack values and slot count are compared.
 * @return True when both stacks have compatible value grouping.
 * @ingroup implementation
 */
[[nodiscard]] bool same_stack_shape(const Frame& a, const Frame& b) {
  return a.stack_slots == b.stack_slots &&
         std::ranges::equal(a.stack, b.stack, {}, &VerificationType::width, &VerificationType::width);
}

} // namespace

std::size_t JoinSearch::KeyHash::operator()(const Key& key) const noexcept {
  std::size_t hash = NameHash{}(key.left);
  const auto mix = [&](std::size_t value) {
    hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  };
  mix(NameHash{}(key.right));
  mix(key.slot.block_entry.index);
  mix(key.slot.block_entry.generation);
  mix(key.slot.index);
  mix(key.slot.local);
  return hash;
}

std::string_view JoinSearch::select(JoinSlot slot, std::string_view left, std::string_view right,
                                    const classpath::JoinResult& common) {
  const auto& candidates = common.ambiguous_candidates;
  if (candidates.empty())
    return common.type;

  // A candidate can already be the fallback superclass.
  const auto duplicate = std::ranges::find(candidates, common.type);
  const auto alternatives = candidates.size() - (duplicate != candidates.end() ? 1u : 0u);
  if (alternatives == 0)
    return common.type;

  const auto [first, second] = std::minmax(left, right);
  const auto existing = indices.find(Key{slot, first, second});
  if (existing == indices.end()) {
    const Key key{slot, intern_name(names, first), intern_name(names, second)};
    indices.emplace(key, choices.size());
    choices.push_back({key, 0, alternatives});
    return common.type;
  }
  const auto selected = choices[existing->second].selected;
  if (selected == 0 || selected > alternatives)
    return common.type;
  auto index = selected - 1;
  if (static_cast<std::size_t>(duplicate - candidates.begin()) <= index)
    ++index;
  return candidates[index];
}

bool JoinSearch::advance() {
  // Advance the latest pending choice. Earlier choices replay the same
  // prefix. The new choice can change which joins occur afterward.
  while (!choices.empty()) {
    auto& last = choices.back();
    if (last.selected < last.alternatives) {
      ++last.selected;
      return true;
    }
    indices.erase(last.key);
    choices.pop_back();
  }
  return false;
}

std::expected<VerificationType, AnalysisError> FrameMerger::join(
    VerificationType left, VerificationType right, JoinSlot slot) {
  if (left == right)
    return left;
  if (left.kind == Null && right.kind == Object)
    return right;
  if (left.kind == Object && right.kind == Null)
    return left;
  if (left.kind == Object && right.kind == Object) {
    if (!hierarchy)
      return VerificationType{.kind = Object};
    auto common = hierarchy->join_reference_types(left.reference, right.reference);
    if (!common)
      return unresolved(std::move(common.error()));
    const auto chosen = search.select(slot, left.reference, right.reference, *common);
    return VerificationType{.kind = Object, .reference = intern_name(names, chosen)};
  }
  if (slot.local)
    return VerificationType{.kind = Top};
  return incompatible("operand stacks contain incompatible verification types or allocation sites");
}

std::expected<bool, AnalysisError> FrameMerger::merge(Frame& target, const Frame& incoming, InstructionId block_entry) {
  if (!same_stack_shape(target, incoming))
    return incompatible("incoming operand stacks have different slot counts or category shapes");
  bool changed = incoming.constructor_uninitialized && !target.constructor_uninitialized;
  target.constructor_uninitialized |= incoming.constructor_uninitialized;

  const auto merge_values = [&](auto& to, const auto& from, bool local) -> std::expected<void, AnalysisError> {
    for (std::size_t i = 0; i < to.size(); ++i) {
      auto joined = join(to[i], from[i], JoinSlot{block_entry, i, local});
      if (!joined)
        return std::unexpected(std::move(joined.error()));
      changed |= to[i] != *joined;
      to[i] = *joined;
    }
    return {};
  };

  if (auto merged = merge_values(target.stack, incoming.stack, false); !merged)
    return std::unexpected(std::move(merged.error()));
  if (auto merged = merge_values(target.locals, incoming.locals, true); !merged)
    return std::unexpected(std::move(merged.error()));

  // Slot-wise joins can retain a continuation after its head became Top.
  // A surviving wide head must also have a surviving continuation.
  for (std::size_t i = 0; i < target.locals.size(); ++i) {
    auto& local = target.locals[i];
    if ((local.kind == WideContinuation && (i == 0 || target.locals[i - 1].width() != 2)) ||
        (local.width() == 2 && (i + 1 == target.locals.size() || target.locals[i + 1].kind != WideContinuation))) {
      local = {};
      changed = true;
    }
  }
  return changed;
}

std::expected<bool, AnalysisError> FrameMerger::assignable(VerificationType from, VerificationType to) const {
  if (from == to || to.kind == Top || (from.kind == Null && to.kind == Object))
    return true;
  if (from.kind != Object || to.kind != Object)
    return false;
  if (!hierarchy)
    return true;
  auto matches = hierarchy->is_assignable(from.reference, to.reference);
  if (!matches)
    return unresolved(std::move(matches.error()));
  return *matches;
}

std::expected<void, AnalysisError> FrameMerger::check(const Frame& target, const Frame& incoming) const {
  if (!same_stack_shape(target, incoming))
    return incompatible("incoming operand stack does not fit the declared frame");
  if (incoming.constructor_uninitialized && !target.constructor_uninitialized)
    return incompatible("uninitialized constructor state cannot enter an initialized frame");

  const auto check_values = [&](const auto& to, const auto& from, const char* reason)
      -> std::expected<void, AnalysisError> {
    for (std::size_t i = 0; i < to.size(); ++i) {
      auto ok = assignable(from[i], to[i]);
      if (!ok)
        return std::unexpected(std::move(ok.error()));
      if (!*ok)
        return incompatible(reason);
    }
    return {};
  };
  if (auto checked = check_values(target.stack, incoming.stack, "operand type does not fit the declared frame"); !checked)
    return checked;
  return check_values(target.locals, incoming.locals, "local variable type does not fit the declared frame");
}

} // namespace cafe::analysis
