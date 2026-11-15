#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

#include "cafe/analysis/analysis_error.hpp"
#include "cafe/analysis/limits.hpp"
#include "cafe/attribute.hpp"

namespace cafe::analysis {

struct BlockId {
  std::uint32_t value;
  friend bool operator==(BlockId, BlockId) = default;
};

enum class EdgeKind : std::uint8_t {
  Fallthrough,
  Conditional,
  Unconditional,
  SwitchCase,
  SwitchDefault,
};

struct Edge {
  BlockId target;
  EdgeKind kind;
  std::optional<std::int32_t> case_key{};
};

struct BasicBlock {
  InstructionId first;
  InstructionId last;
  // Half-open interval in method instruction order.
  std::uint32_t begin;
  std::uint32_t end;
  bool frame_boundary;
  bool reachable;
};

struct ProtectedRange {
  // Half-open interval in method instruction order. Ranges retain exception
  // table order, and each covered instruction has an edge to target.
  std::uint32_t begin;
  std::uint32_t end;
  BlockId target;
  ExceptionHandler handler;
};

class ControlFlowGraph {
public:
  ~ControlFlowGraph();
  ControlFlowGraph(ControlFlowGraph&&) noexcept;
  ControlFlowGraph& operator=(ControlFlowGraph&&) noexcept;
  ControlFlowGraph(const ControlFlowGraph&) = delete;
  ControlFlowGraph& operator=(const ControlFlowGraph&) = delete;

  [[nodiscard]] std::span<const BasicBlock> blocks() const noexcept;
  [[nodiscard]] std::span<const Edge> outgoing(BlockId from) const noexcept;
  [[nodiscard]] std::optional<BlockId> block_of(InstructionId instruction) const;
  [[nodiscard]] std::span<const ProtectedRange> exception_ranges() const noexcept;
  // Replace the contents with indices into exception_ranges(), in table order.
  // All instructions within a block have the same protected ranges.
  void handlers_for(BlockId block, std::pmr::vector<std::uint32_t>& ranges) const;
  [[nodiscard]] std::uint64_t code_revision() const noexcept;
  [[nodiscard]] bool is_current(const MethodCode& code,
                                std::span<const ExceptionHandler> handlers) const noexcept;

private:
  struct Storage;
  explicit ControlFlowGraph(std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage;

  friend std::expected<ControlFlowGraph, AnalysisError> build_cfg(
      const MethodCode&, std::span<const ExceptionHandler>, std::pmr::memory_resource*, const AnalysisLimits&);
};

// The resource must outlive the returned graph. Block zero is the method entry.
[[nodiscard]] std::expected<ControlFlowGraph, AnalysisError> build_cfg(
    const MethodCode& code, std::span<const ExceptionHandler> handlers = {},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource(), const AnalysisLimits& limits = {});

} // namespace cafe::analysis
