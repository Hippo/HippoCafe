/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <utility>

#include "cafe/analysis/limits.hpp"
#include "cafe/classpath/class_hierarchy.hpp"

namespace cafe::analysis {

/**
 * @brief Hierarchy snapshot and limits for frame computation.
 *
 * To write computed frames, register the target runtime and dependencies with
 * classpath::ClassPathBuilder and build a ClassHierarchy from the resulting snapshot.
 * Pass this context to the
 * @ref cafe::write_class(const ClassFile&,const WriteOptions&,analysis::AnalysisContext&) "write_class overload that takes a context"
 * with CodeAnalysisMode::ComputeFrames.
 * The default context starts with an empty classpath.
 * @ingroup analysis
 */
class AnalysisContext {
public:
  /**
   * @brief Create a context with an empty classpath and the supplied limits.
   * @param limits Resource ceilings and cooperative cancellation policy.
   */
  explicit AnalysisContext(AnalysisLimits limits = {});
  /**
   * @brief Create a context from a hierarchy snapshot and the supplied limits.
   * @param hierarchy Reference-resolution hierarchy snapshot used by this analysis context.
   * @param limits Resource ceilings and cooperative cancellation policy.
   */
  explicit AnalysisContext(classpath::ClassHierarchy hierarchy, AnalysisLimits limits = {}) :
      class_hierarchy(std::move(hierarchy)), analysis_limits(std::move(limits)) {
  }

  /**
   * @brief Inspect the resolution hierarchy used for frame computation.
   * @return A borrowed const hierarchy reference.
   */
  [[nodiscard]] const classpath::ClassHierarchy& hierarchy() const noexcept {
    return class_hierarchy;
  }
  /**
   * @brief Inspect the analysis resource and cancellation policy.
   * @return A borrowed const limits reference.
   */
  [[nodiscard]] const AnalysisLimits& limits() const noexcept {
    return analysis_limits;
  }

private:
  classpath::ClassHierarchy class_hierarchy; ///< Hierarchy snapshot used for reference checks.
  AnalysisLimits analysis_limits; ///< Per-analysis work, memory, and cancellation policy.
};

} // namespace cafe::analysis
