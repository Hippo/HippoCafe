#pragma once

#include <cstdint>
#include <expected>
#include <span>

#include "cafe/analysis/analysis_error.hpp"
#include "cafe/analysis/limits.hpp"

namespace cafe {
class ClassFile;
class Method;
class MethodCode;
struct ExceptionHandler;

namespace analysis {

// Checks control flow and stack categories without a classpath. Use
// compute_frames when reference assignability must also be established.
[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const AnalysisLimits& limits = {});

[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, const AnalysisLimits& limits = {});

} // namespace analysis
} // namespace cafe
