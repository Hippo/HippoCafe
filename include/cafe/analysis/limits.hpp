/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <cstddef>
#include <memory_resource>
#include <stop_token>

namespace cafe::analysis {

/**
 * @brief Per-analysis work, memory, slot, and frame ceilings with cooperative cancellation. The
 * upstream resource must outlive returned analysis storage.
 * @ingroup analysis
 */
struct AnalysisLimits {
  std::size_t max_instructions = 65535; ///< Maximum instructions in an analyzed method.
  std::size_t max_handlers = 65535; ///< Maximum exception-table entries in an analyzed method.
  std::size_t max_edges = 262144; ///< Maximum normal and indexed exceptional CFG edge budget.
  std::size_t max_work = std::size_t{32} * 1024 * 1024; ///< Maximum charged analysis work units, including propagation and candidate search.
  std::size_t max_memory_bytes = std::size_t{128} * 1024 * 1024; ///< Maximum charged analysis storage and scratch bytes.
  std::size_t max_stack_slots = 65535; ///< Maximum operand-stack size in JVM slots.
  std::size_t max_local_slots = 65535; ///< Maximum local-variable count in JVM slots.
  std::size_t max_frames = 65535; ///< Maximum materialized stack-map frames.
  /**
   * @brief Borrowed allocator that must outlive returned owner/analysis storage.
   *
   * Used by frame and maxima computation. The resource must outlive the
   * analysis and any returned FrameAnalysis.
   */
  std::pmr::memory_resource* upstream_resource = std::pmr::get_default_resource();
  std::stop_token cancellation{}; ///< Cooperative stop token checked during bounded work.
};

} // namespace cafe::analysis
