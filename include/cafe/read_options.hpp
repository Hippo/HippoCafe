#pragma once

#include <cstddef>
#include <memory_resource>

#include <cafe/class_version.hpp>

namespace cafe {

enum class ParseMode { Strict,
                       Compatible };

struct ReadLimits {
  std::size_t max_input_bytes = 64 * 1024 * 1024;
  std::size_t max_total_allocated_bytes = 256 * 1024 * 1024;
  std::size_t max_attributes = 100'000;
  std::size_t max_annotation_depth = 128;
  std::size_t max_switch_cases = MAX_CODE_SIZE / 4;
  std::size_t max_methods = 65535;
  std::size_t max_code_bytes = MAX_CODE_SIZE;
  std::size_t max_diagnostics = 1024;
};

struct ReadOptions {
  ParseMode mode = ParseMode::Compatible;
  ReadLimits limits = {};
  bool allow_opaque_code = false;
  // The upstream resource must outlive the returned ClassFile.
  std::pmr::memory_resource* upstream_resource = std::pmr::get_default_resource();
};

} // namespace cafe
