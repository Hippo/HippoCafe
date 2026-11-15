/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

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

/**
 * @brief Compute stack-slot maxima and check stack categories without a hierarchy.
 *
 * Reference assignability requires compute_frames(). The explicit-code overload analyzes a writer
 * emission plan against the same owning method descriptor and constant pool.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param method Method belonging to the supplied class.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return The required slot count, or an invalid-control-flow/category/resource error.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const AnalysisLimits& limits = {});

/**
 * @brief Compute stack-slot maxima and check stack categories without a hierarchy.
 *
 * Reference assignability requires compute_frames(). The explicit-code overload analyzes a writer
 * emission plan against the same owning method descriptor and constant pool.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param method Method belonging to the supplied class.
 * @param code Decoded method instruction sequence to inspect.
 * @param handlers Exception-table entries in class-file order using the supplied code's labels.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return The required slot count, or an invalid-control-flow/category/resource error.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<std::uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, const AnalysisLimits& limits = {});

} // namespace analysis
} // namespace cafe
