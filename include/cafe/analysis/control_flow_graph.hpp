/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

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

/**
 * @brief Zero-based block index scoped to one ControlFlowGraph; block zero is the method entry.
 * @ingroup analysis
 */
struct BlockId {
  std::uint32_t value; ///< Zero-based block-table index.
  friend bool operator==(BlockId, BlockId) = default;
};

/**
 * @brief Normal CFG edge classification; exception-table edges are indexed separately.
 * @ingroup analysis
 */
enum class EdgeKind : std::uint8_t {
  Fallthrough, ///< Normal execution continues at the next block.
  Conditional, ///< Taken edge of a conditional branch.
  Unconditional, ///< Unconditional jump target.
  SwitchCase, ///< Explicit switch case with a signed key.
  SwitchDefault, ///< Switch default destination.
};

/**
 * @brief Normal control-flow successor, optionally carrying a signed switch key. Exceptional
 * successors are represented by ProtectedRange.
 * @ingroup analysis
 */
struct Edge {
  BlockId target; ///< Destination block in the owning control-flow graph.
  EdgeKind kind; ///< Discriminator selecting the interpretation of this record.
  std::optional<std::int32_t> case_key{}; ///< Signed key for a SwitchCase edge; empty for other edge kinds.
};

/**
 * @brief Half-open instruction-order interval with inclusive first/last instruction handles and
 * reachability/frame-boundary flags.
 * @ingroup analysis
 */
struct BasicBlock {
  InstructionId first; ///< First instruction in the block, inclusive.
  InstructionId last; ///< Last instruction in the block, inclusive.
  /**
   * @brief Inclusive instruction-order position.
   */
  std::uint32_t begin;
  std::uint32_t end; ///< Exclusive instruction-order position.
  bool frame_boundary; ///< Whether a verifier entry frame may be required at this block.
  bool reachable; ///< Whether the block is reachable from the method entry or reachable handlers.
};

/**
 * @brief Half-open protected interval in instruction order, retaining exception-table order and
 * its handler target.
 * @ingroup analysis
 */
struct ProtectedRange {
  /**
   * @brief Inclusive instruction-order position.
   *
   * Half-open interval in method instruction order. Ranges retain exception
   * table order, and each covered instruction has an edge to target.
   */
  std::uint32_t begin;
  std::uint32_t end; ///< Exclusive instruction-order position.
  BlockId target; ///< Handler entry block in the owning control-flow graph.
  ExceptionHandler handler; ///< Original exception-table entry for this range.
};

/**
 * @brief Owned CFG snapshot with normal edges and indexed exception ranges. Views remain valid
 * until destruction or move assignment. The allocator must outlive the graph; is_current() detects
 * code/handler changes.
 * @ingroup analysis
 */
class ControlFlowGraph {
public:
  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~ControlFlowGraph();
  /**
   * @brief Transfer ownership without relocating the backing storage; existing references follow the
   * destination owner.
   */
  ControlFlowGraph(ControlFlowGraph&&) noexcept;
  /**
   * @brief Replace owned state by move assignment; views of the previous destination state are
   * invalidated.
   * @return This object after ownership transfer.
   */
  ControlFlowGraph& operator=(ControlFlowGraph&&) noexcept;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ControlFlowGraph(const ControlFlowGraph&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ControlFlowGraph& operator=(const ControlFlowGraph&) = delete;

  /**
   * @brief Inspect blocks in instruction order.
   * @return A graph-owned span valid until destruction or move assignment.
   */
  [[nodiscard]] std::span<const BasicBlock> blocks() const noexcept;
  /**
   * @brief Inspect normal successors of a block.
   * @param from Zero-based block identity belonging to the supplied graph.
   * @return A borrowed edge span; invalid block IDs return an empty span.
   */
  [[nodiscard]] std::span<const Edge> outgoing(BlockId from) const noexcept;
  /**
   * @brief Locate the block containing an instruction.
   * @param instruction Stable instruction handle belonging to the supplied method.
   * @return A block ID, or nullopt for a foreign/stale/unrecognized instruction.
   */
  [[nodiscard]] std::optional<BlockId> block_of(InstructionId instruction) const;
  /**
   * @brief Inspect exception-table ranges in table order.
   * @return A graph-owned span of protected intervals and targets.
   */
  [[nodiscard]] std::span<const ProtectedRange> exception_ranges() const noexcept;
  /**
   * @brief Replace an output vector with indices of ranges protecting a block, in table order.
   * @param block Block in this graph; an out-of-range ID produces an empty result.
   * @param ranges Output vector replaced with exception-range indices in table order.
   *
   * Replace the contents with indices into exception_ranges(), in table order.
   * All instructions within a block have the same protected ranges.
   */
  void handlers_for(BlockId block, std::pmr::vector<std::uint32_t>& ranges) const;
  /**
   * @brief Read the instruction revision captured by the graph.
   * @return The snapshot revision.
   */
  [[nodiscard]] std::uint64_t code_revision() const noexcept;
  /**
   * @brief Check that instruction identity/revision and the exception table still match.
   * @param code Decoded method instruction sequence to inspect.
   * @param handlers Exception-table entries in class-file order using the supplied code's labels.
   * @return True only for the exact unchanged code and handler snapshot.
   */
  [[nodiscard]] bool is_current(const MethodCode& code,
                                std::span<const ExceptionHandler> handlers) const noexcept;

private:
  struct Storage;
  /**
   * @brief Take ownership of constructed graph storage.
   * @param storage Owned implementation storage transferred into this object.
   */
  explicit ControlFlowGraph(std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage; ///< Owned backing storage for this object and its borrowed views.

  // The namespace-level declaration below owns the contract. Extracting this duplicate
  // friend declaration makes Doxygen generate a member link without a matching anchor.
  /** @cond duplicate_friend_declarations */
  friend std::expected<ControlFlowGraph, AnalysisError> build_cfg(
      const MethodCode&, std::span<const ExceptionHandler>, std::pmr::memory_resource*, const AnalysisLimits&);
  /** @endcond */
};

/**
 * @brief Build basic blocks, normal edges, and indexed exception ranges from decoded code.
 *
 * Block zero is the entry. The resource must outlive the returned graph. The graph snapshots code
 * identity and handler order; it does not mutate instructions.
 * @param code Decoded method instruction sequence to inspect.
 * @param handlers Exception-table entries in class-file order using the supplied code's labels.
 * @param resource PMR resource that must outlive the allocated object or returned storage.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return An owned graph, or an invalid-code/label/range/resource/cancellation error.
 * @ingroup analysis
 */
[[nodiscard]] std::expected<ControlFlowGraph, AnalysisError> build_cfg(
    const MethodCode& code, std::span<const ExceptionHandler> handlers = {},
    std::pmr::memory_resource* resource = std::pmr::get_default_resource(), const AnalysisLimits& limits = {});

} // namespace cafe::analysis
