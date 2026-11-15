#pragma once

#include <cstddef>
#include <memory_resource>
#include <stop_token>

namespace cafe::analysis {

struct AnalysisLimits {
  std::size_t max_instructions = 65535;
  std::size_t max_handlers = 65535;
  std::size_t max_edges = 262144;
  std::size_t max_work = 32 * 1024 * 1024;
  std::size_t max_memory_bytes = 128 * 1024 * 1024;
  std::size_t max_stack_slots = 65535;
  std::size_t max_local_slots = 65535;
  std::size_t max_frames = 65535;
  // Used by frame and maxima computation. The resource must outlive the
  // analysis and any returned FrameAnalysis.
  std::pmr::memory_resource* upstream_resource = std::pmr::get_default_resource();
  std::stop_token cancellation{};
};

} // namespace cafe::analysis
