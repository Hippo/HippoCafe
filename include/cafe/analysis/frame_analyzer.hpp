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

class FrameAnalysis {
public:
  ~FrameAnalysis();
  FrameAnalysis(FrameAnalysis&&) noexcept;
  FrameAnalysis& operator=(FrameAnalysis&&) noexcept;
  FrameAnalysis(const FrameAnalysis&) = delete;
  FrameAnalysis& operator=(const FrameAnalysis&) = delete;

  [[nodiscard]] std::uint16_t max_stack() const noexcept;
  [[nodiscard]] std::uint16_t max_locals() const noexcept;
  [[nodiscard]] std::uint64_t code_revision() const noexcept;
  // Entries follow instruction order, with unused trailing local slots
  // omitted. Values and reference names remain valid until destruction.
  [[nodiscard]] std::span<const StackMapFrame> frames() const noexcept;
  // Checks code and exception table identity for a writer emission plan.
  [[nodiscard]] bool is_current(const MethodCode& code,
                                std::span<const ExceptionHandler> handlers) const noexcept;
  // Also checks that the owner declarations and hierarchy snapshot match.
  [[nodiscard]] bool is_current(const ClassFile& owner, const Method& method,
                                const AnalysisContext& context) const noexcept;

private:
  struct Storage;
  explicit FrameAnalysis(std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage;

  friend class FrameEngine;
};

// Supports class-file majors 45–69. For ambiguous reference joins, the
// verifier first tries a common superclass, then a bounded set of common
// candidates in deterministic order. Each attempt rechecks downstream uses.
[[nodiscard]] std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context);

// Analyze an emission plan with the same method descriptor and constant pool.
// Additional frame points must already be block entries. A branch expansion
// can introduce a required frame at the original conditional fallthrough.
[[nodiscard]] std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner, const Method& method, AnalysisContext& context,
    const MethodCode& code, std::span<const ExceptionHandler> handlers,
    std::span<const InstructionId> extra_frame_points = {});

} // namespace analysis
} // namespace cafe
