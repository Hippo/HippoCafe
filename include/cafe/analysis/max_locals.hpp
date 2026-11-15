#pragma once

#include <cstdint>
#include <expected>

#include "cafe/analysis/analysis_error.hpp"
#include "cafe/analysis/limits.hpp"

namespace cafe {
class ClassFile;
class Method;
class MethodCode;

namespace analysis {

[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const AnalysisLimits& limits = {});

[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const MethodCode& code, const AnalysisLimits& limits = {});

} // namespace analysis
} // namespace cafe
