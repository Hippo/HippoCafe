/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "cafe/analysis/analysis_error.hpp"
#include "cafe/analysis/context.hpp"
#include "cafe/analysis/frame.hpp"
#include "cafe/attribute.hpp"

namespace cafe {
class ClassFile;
class Method;

namespace analysis {

/**
 * @brief Owned derived maxima and entry frames for a method/context snapshot. Names and frame
 * views remain valid until destruction or move assignment. Check is_current() before reuse after
 * edits or hierarchy changes.
 * @ingroup analysis
 */
class FrameAnalysis {
public:
  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~FrameAnalysis();
  /**
   * @brief Transfer ownership without relocating the backing storage; existing references follow the
   * destination owner.
   */
  FrameAnalysis(FrameAnalysis&&) noexcept;
  /**
   * @brief Replace owned state by move assignment; views of the previous destination state are
   * invalidated.
   * @return This object after ownership transfer.
   */
  FrameAnalysis& operator=(FrameAnalysis&&) noexcept;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  FrameAnalysis(const FrameAnalysis&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  FrameAnalysis& operator=(const FrameAnalysis&) = delete;

  /**
   * @brief Read the computed maximum operand-stack size.
   * @return The number of JVM stack slots required by the analyzed method.
   */
  [[nodiscard]] std::uint16_t max_stack() const noexcept;
  /**
   * @brief Read the computed maximum local-variable size.
   * @return The number of JVM local slots required by the analyzed method.
   */
  [[nodiscard]] std::uint16_t max_locals() const noexcept;
  /**
   * @brief Read the instruction revision captured by analysis.
   * @return The analyzed code revision.
   */
  [[nodiscard]] std::uint64_t code_revision() const noexcept;
  /**
   * @brief Inspect emitted entry-frame states in instruction order.
   * @return A result-owned span; reference names and values remain valid until destruction or move
   *   assignment.
   *
   * Entries follow instruction order, with unused trailing local slots
   * omitted. Values and reference names remain valid until destruction.
   */
  [[nodiscard]] std::span<const StackMapFrame> frames() const noexcept;
  /**
   * @brief Check whether this analysis still matches its input snapshot.
   * @param code Decoded method instruction sequence to inspect.
   * @param handlers Exception-table entries in class-file order using the supplied code's labels.
   * @return True when code/handlers match; the owner/method/context overload additionally checks
   *   declarations and hierarchy identity.
   *
   * Checks code and exception table identity for a writer emission plan.
   */
  [[nodiscard]] bool is_current(const MethodCode& code,
                                std::span<const ExceptionHandler> handlers) const noexcept;
  /**
   * @brief Check whether this analysis still matches its input snapshot.
   * @param owner Class owning the method, constants, or source slices being inspected.
   * @param method Method belonging to the supplied class.
   * @param context Hierarchy and resource policy for verification computation.
   * @return True when code/handlers match; the owner/method/context overload additionally checks
   *   declarations and hierarchy identity.
   *
   * Also checks that the owner declarations and hierarchy snapshot match.
   */
  [[nodiscard]] bool is_current(const ClassFile& owner, const Method& method,
                                const AnalysisContext& context) const noexcept;

private:
  struct Storage;
  /**
   * @brief Take ownership of completed verifier results and their backing storage.
   * @param storage Owned implementation storage transferred into this object.
   */
  explicit FrameAnalysis(std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage; ///< Owned backing storage for this object and its borrowed views.

  friend class FrameEngine;
};

/**
 * @brief Compute max stack, max locals, and verifier entry frames for one decoded method.
 *
 * Inspect the result in memory. To serialize a class with computed frames, call the
 * @ref cafe::write_class(const ClassFile&,const WriteOptions&,analysis::AnalysisContext&) "write_class overload that takes a context"
 * with CodeAnalysisMode::ComputeFrames.
 *
 * Supports class-file major versions 45 through 69. Legacy subroutines and modern unreachable
 * frame regions return structured errors. Ambiguous reference joins try the common superclass
 * followed by bounded alternatives and check downstream uses at each attempt.
 * Allocation failures may propagate.
 * @param owner Class that owns the method. Its current declarations take precedence during resolution.
 * @param method Decoded method belonging to the supplied class.
 * @param context Analysis limits and hierarchy containing the target runtime and dependencies
 *   needed for reference resolution.
 * @return An owned FrameAnalysis, or a structured verification, resolution, resource, or
 *   cancellation failure.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context);

/**
 * @brief Compute frames for an explicit instruction sequence and exception table.
 *
 * Use this overload to analyze an emission plan while retaining the method descriptor and
 * class constant pool. For example, an expanded conditional branch can require a frame at
 * the original fallthrough. The same verification rules apply as for the decoded-method overload.
 * Allocation failures may propagate.
 * @param owner Class owning the method, constants, or source slices being inspected.
 * @param method Method belonging to the supplied class.
 * @param context Hierarchy and resource policy for verification computation.
 * @param code Decoded method instruction sequence to inspect.
 * @param handlers Exception-table entries in class-file order using the supplied code's labels.
 * @param extra_frame_points Additional required frame boundaries, each already a block entry.
 * @return An owned FrameAnalysis, or a structured verification, resolution, resource, or
 *   cancellation failure.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context,
    const MethodCode& code, std::span<const ExceptionHandler> handlers,
    std::span<const InstructionId> extra_frame_points = {});

} // namespace analysis
} // namespace cafe
