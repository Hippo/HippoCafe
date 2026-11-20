/**
 * @file
 * @brief Declared verifier states and typed basic-block entries used by SSA.
 * @ingroup implementation
 */
#pragma once

#include "cafe/analysis/control_flow_graph.hpp"
#include "cafe/analysis/frame_analyzer.hpp"

namespace cafe::analysis {

/// Every verified block entry in instruction order. All views borrow the owned analysis.
struct SsaFrames {
  FrameAnalysis analysis; ///< Owner of frame records and interned reference names.
  std::span<const StackMapFrame> entries; ///< Block-entry states in instruction order.
  std::uint16_t max_locals; ///< Width of each block's local-slot array.
  std::uint16_t max_stack; ///< Maximum operand-stack width in JVM slots.
};

/// Expanded StackMapTable with stable allocation identities and owned reference names.
struct DeclaredFrames {
  struct Storage;
  std::unique_ptr<Storage> storage; ///< Backing allocations retained across moves.
  std::span<const StackMapFrame> entries; ///< Expanded entries borrowing the backing storage.
  /// Construct an empty declaration set for a method with no current table.
  DeclaredFrames();
  /// Release all retained verifier declarations.
  ~DeclaredFrames();
  /// Transfer ownership of the expanded table.
  DeclaredFrames(DeclaredFrames&&) noexcept;
  /// Replace the current declarations by move.
  /// @return This object after the transfer.
  DeclaredFrames& operator=(DeclaredFrames&&) noexcept;
};

/// Decode a current StackMapTable using its actual bytecode layout.
/// @param owner Class supplying constant and declaration identities.
/// @param method Decoded method whose current Code metadata is inspected.
/// @param limits Resource ceilings and cooperative cancellation token.
/// @return Owned expanded declarations, empty for missing/stale metadata, or a decoding failure.
[[nodiscard]] std::expected<DeclaredFrames, AnalysisError> decode_ssa_frames(
    const ClassFile& owner, const Method& method, const AnalysisLimits& limits);

/// Build the ordinary CFG with extra block boundaries at declared stack map offsets.
/// @param code Logical instructions to partition.
/// @param handlers Ordered exception table for these instructions.
/// @param seeds Declared frames at additional instruction boundaries.
/// @param resource Upstream allocator that outlives the result.
/// @param limits Graph construction limits.
/// @return The graph or a structural, resource, or cancellation error.
[[nodiscard]] std::expected<ControlFlowGraph, AnalysisError> ssa_cfg(
    const MethodCode& code, std::span<const ExceptionHandler> handlers,
    std::span<const StackMapFrame> seeds, std::pmr::memory_resource* resource, const AnalysisLimits& limits);

/// Verify all incoming transitions and provide entry states for symbolic simulation.
/// @param owner Class supplying constants and declarations.
/// @param method Method supplying its signature and access flags.
/// @param code Logical method body to verify.
/// @param handlers Ordered exception table for this body.
/// @param context Resolution hierarchy and analysis limits.
/// @param seeds Optional fixed verifier states, including roots of disconnected code.
/// @return Typed entry states, or a verification, resource, or resolution error.
[[nodiscard]] std::expected<SsaFrames, AnalysisError> ssa_frames(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, AnalysisContext& context,
    std::span<const StackMapFrame> seeds = {});

/// Verify a lowered body with typed roots and retain its required wire frames.
/// @param owner Class supplying constants and declarations.
/// @param method Method supplying its signature and access flags.
/// @param code Final logical instruction stream.
/// @param handlers Ordered exception table for that stream.
/// @param context Resolution hierarchy and analysis limits.
/// @param seeds Relocated frame constraints belonging to the final instruction stream.
/// @return Emittable verifier frames and derived maxima, or an analysis error.
[[nodiscard]] std::expected<FrameAnalysis, AnalysisError> emission_frames(
    const ClassFile& owner, const Method& method, const MethodCode& code,
    std::span<const ExceptionHandler> handlers, AnalysisContext& context,
    std::span<const StackMapFrame> seeds);

} // namespace cafe::analysis
