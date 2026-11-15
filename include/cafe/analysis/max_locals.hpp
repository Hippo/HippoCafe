/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

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

/**
 * @brief Compute local slots required by parameters and all local-variable instructions.
 * @param method Method belonging to the supplied class.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return The required slot count, or an invalid-method/version/code/resource error.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const AnalysisLimits& limits = {});

/**
 * @brief Compute local slots required by parameters and all local-variable instructions.
 * @param method Method belonging to the supplied class.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param code Decoded method instruction sequence to inspect.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return The required slot count, or an invalid-method/version/code/resource error.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const MethodCode& code, const AnalysisLimits& limits = {});

} // namespace analysis
} // namespace cafe
