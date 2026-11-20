/**
 * @file
 * @brief Physical JVM local assignment for SSA destruction.
 * @ingroup implementation
 */
#pragma once

#include "ssa_detail.hpp"

#include <limits>

namespace cafe::analysis {

/// Sentinel for values that never require a physical local.
inline constexpr std::uint32_t SSA_NO_SLOT = std::numeric_limits<std::uint32_t>::max();

/// Cooperative budget shared by local allocation, copy scheduling and bytecode generation.
struct SsaLowerBudget {
  const AnalysisLimits& limits; ///< Cancellation and per-analysis ceilings.
  std::size_t remaining; ///< Work units left in this lowering.

  /**
   * @brief Charge the next bounded unit of work.
   * @param amount Work units to deduct.
   * @return Success, cancellation, or configured exhaustion.
   */
  [[nodiscard]] SsaResult<void> spend(std::size_t amount) {
    if (limits.cancellation.stop_requested())
      return std::unexpected(SsaError{SsaErrorKind::Cancelled, "SSA lowering cancelled"});
    if (amount > remaining)
      return std::unexpected(SsaError{SsaErrorKind::ResourceLimitExceeded, "SSA lowering work budget exhausted"});
    remaining -= amount;
    return {};
  }
};

/// Home assignments and stack-resident producers, indexed by graph value slot.
struct SsaSlots {
  /**
   * @brief Construct empty slot assignment arrays.
   * @param resource Borrowed allocator for the duration of lowering.
   */
  explicit SsaSlots(std::pmr::memory_resource* resource) : homes(resource), carried(resource), needed(resource), capture_exception(resource) {
  }
  std::pmr::vector<std::uint32_t> homes; ///< Physical slot per graph value, or SSA_NO_SLOT.
  std::pmr::vector<bool> carried; ///< Single-use values transported on the JVM operand stack.
  std::pmr::vector<bool> needed; ///< Definitions required by emitted operations, edges or debug writes.
  std::pmr::vector<bool> capture_exception; ///< Edge-defined exceptions requiring a JVM home beyond their entry copies.
  std::uint32_t count{}; ///< Number of local slots reserved before parallel-copy scratch.
};

/**
 * @brief Compute conservative live intervals over ordinary and instruction-specific exceptional edges.
 * @param graph Verified graph supplying value definitions and incoming transfers.
 * @param reserved Leading physical slots reserved for receiver, arguments or visible source locals.
 * @param mirrored_homes Map from source slot to debugger home, or SSA_NO_SLOT when it needs no mirror.
 * @param budget Work and cancellation state shared with bytecode generation.
 * @param scratch Borrowed allocator for assignments and temporary liveness state.
 * @return Assignment indexed by graph value, or a slot, work or cancellation failure.
 */
[[nodiscard]] SsaResult<SsaSlots> assign_ssa_slots(const SsaStorage& graph, std::size_t reserved,
                                                   std::span<const std::uint32_t> mirrored_homes, SsaLowerBudget& budget,
                                                   std::pmr::memory_resource* scratch);

} // namespace cafe::analysis
