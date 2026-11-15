/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

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

/**
 * @brief Borrowed class/method identity views used to attach context to analysis errors. The
 * owning class must remain alive.
 * @ingroup implementation
 */
struct MethodSite {
  std::string_view class_name; ///< Internal name of the affected class.
  std::string_view name; ///< JVM internal or declaration name in modified UTF-8.
  std::string_view descriptor; ///< JVM field or method descriptor.

  /**
   * @brief Attach borrowed method identity to an owned error without overwriting a more precise
   * instruction location.
   * @param issue Structured failure to enrich with method identity.
   * @param at Existing insertion anchor or instruction location.
   * @return An AnalysisError with owned class/name/descriptor strings.
   */
  [[nodiscard]] AnalysisError error(AnalysisError issue, std::optional<InstructionId> at = {}) const {
    issue.class_name = class_name;
    issue.method_name = name;
    issue.descriptor = descriptor;
    if (!issue.instruction)
      issue.instruction = at;
    return issue;
  }

  /**
   * @brief Construct the operation's structured failure while owning diagnostic text.
   * @param kind Payload or error discriminator.
   * @param reason Explanation copied into the structured failure.
   * @param at Existing insertion anchor or instruction location.
   * @return An unexpected result carrying the appropriate error classification and location.
   */
  [[nodiscard]] std::unexpected<AnalysisError> fail(
      AnalysisErrorKind kind, std::string_view reason, std::optional<InstructionId> at = {}) const {
    return std::unexpected(error({.kind = kind, .detail = std::string(reason)}, at));
  }
};

/**
 * @brief Borrow class/method identity strings for subsequent analysis diagnostics.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param method Method belonging to the supplied class.
 * @return A MethodSite with empty views for unavailable identity strings.
 * @ingroup implementation
 */
[[nodiscard]] inline MethodSite method_site(const ClassFile& owner, const Method& method) {
  const auto& pool = owner.constant_pool();
  return {detail::class_name(pool, owner.this_class()).value_or(std::string_view{}),
          pool.get_utf8(method.name_index()).value_or(std::string_view{}),
          pool.get_utf8(method.descriptor_index()).value_or(std::string_view{})};
}

/**
 * @brief Check whether a method reference belongs to the supplied class's current table.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param method Method belonging to the supplied class.
 * @return True for an address inside the current method span.
 * @ingroup implementation
 */
[[nodiscard]] inline bool owns_method(const ClassFile& owner, const Method& method) {
  const auto methods = owner.methods();
  const std::less<const Method*> before;
  return !methods.empty() && !before(&method, methods.data()) &&
         before(&method, methods.data() + methods.size());
}

/**
 * @brief Copy verification state while retaining the destination vectors' allocators.
 * @param destination Frame whose state is replaced while retaining its allocator bindings.
 * @param source Source frame whose values are copied into the destination allocator.
 * @ingroup implementation
 */
inline void copy_frame(Frame& destination, const Frame& source) {
  destination.locals = source.locals;
  destination.stack = source.stack;
  destination.stack_slots = source.stack_slots;
  destination.constructor_uninitialized = source.constructor_uninitialized;
}

/**
 * @brief Transparent hash allowing borrowed string views to query owned name sets without
 * allocating.
 * @ingroup implementation
 */
struct NameHash {
  /**
   * @brief Enables heterogeneous lookup using borrowed names or pair views.
   */
  using is_transparent = void;
  /**
   * @brief Hash borrowed text using the same hash as owned-name lookups.
   * @param name Borrowed JVM name in modified UTF-8.
   * @return The deterministic standard string-view hash for this process.
   */
  [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept {
    return std::hash<std::string_view>{}(name);
  }
};

/**
 * @brief PMR-owned interned reference names with transparent lookup.
 */
using NameSet = std::pmr::unordered_set<std::pmr::string, NameHash, std::equal_to<>>;

/**
 * @brief Copy a reference name into stable analysis-owned intern storage if absent.
 * @param names Stable owned reference-name intern set.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return A view valid for the lifetime of the name set.
 * @ingroup implementation
 */
[[nodiscard]] inline std::string_view intern_name(NameSet& names, std::string_view name) {
  if (name.empty())
    return {};
  const auto found = names.find(name);
  return found == names.end() ? std::string_view(*names.emplace(name).first) : std::string_view(*found);
}

} // namespace cafe::analysis
