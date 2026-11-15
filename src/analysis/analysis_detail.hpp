#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "cafe/analysis/analysis_error.hpp"
#include "cafe/analysis/frame.hpp"
#include "cafe/class_file.hpp"
#include "io/class_format.hpp"

namespace cafe::analysis {

struct MethodSite {
  std::string_view class_name;
  std::string_view name;
  std::string_view descriptor;

  [[nodiscard]] AnalysisError error(AnalysisError issue, std::optional<InstructionId> at = {}) const {
    issue.class_name = class_name;
    issue.method_name = name;
    issue.descriptor = descriptor;
    if (!issue.instruction)
      issue.instruction = at;
    return issue;
  }

  [[nodiscard]] std::unexpected<AnalysisError> fail(
      AnalysisErrorKind kind, std::string_view reason, std::optional<InstructionId> at = {}) const {
    return std::unexpected(error({.kind = kind, .detail = std::string(reason)}, at));
  }
};

[[nodiscard]] inline MethodSite method_site(const ClassFile& owner, const Method& method) {
  const auto& pool = owner.constant_pool();
  return {detail::class_name(pool, owner.this_class()).value_or(std::string_view{}),
          pool.get_utf8(method.name_index()).value_or(std::string_view{}),
          pool.get_utf8(method.descriptor_index()).value_or(std::string_view{})};
}

[[nodiscard]] inline bool owns_method(const ClassFile& owner, const Method& method) {
  const auto methods = owner.methods();
  const std::less<const Method*> before;
  return !methods.empty() && !before(&method, methods.data()) &&
         before(&method, methods.data() + methods.size());
}

inline void copy_frame(Frame& destination, const Frame& source) {
  destination.locals = source.locals;
  destination.stack = source.stack;
  destination.stack_slots = source.stack_slots;
  destination.constructor_uninitialized = source.constructor_uninitialized;
}

struct NameHash {
  using is_transparent = void;
  [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept {
    return std::hash<std::string_view>{}(name);
  }
};

using NameSet = std::pmr::unordered_set<std::pmr::string, NameHash, std::equal_to<>>;

[[nodiscard]] inline std::string_view intern_name(NameSet& names, std::string_view name) {
  if (name.empty())
    return {};
  const auto found = names.find(name);
  return found == names.end() ? std::string_view(*names.emplace(name).first) : std::string_view(*found);
}

} // namespace cafe::analysis
