#pragma once

#include <utility>

#include "cafe/analysis/limits.hpp"
#include "cafe/classpath/class_hierarchy.hpp"

namespace cafe::analysis {

class AnalysisContext {
public:
  explicit AnalysisContext(AnalysisLimits limits = {});
  explicit AnalysisContext(classpath::ClassHierarchy hierarchy, AnalysisLimits limits = {}) :
      class_hierarchy(std::move(hierarchy)), analysis_limits(limits) {
  }

  [[nodiscard]] const classpath::ClassHierarchy& hierarchy() const noexcept {
    return class_hierarchy;
  }
  [[nodiscard]] const AnalysisLimits& limits() const noexcept {
    return analysis_limits;
  }

private:
  classpath::ClassHierarchy class_hierarchy;
  AnalysisLimits analysis_limits;
};

} // namespace cafe::analysis
