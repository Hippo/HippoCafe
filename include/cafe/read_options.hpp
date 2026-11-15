/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstddef>
#include <memory_resource>

#include <cafe/class_version.hpp>

namespace cafe {

/**
 * @brief Policy for bounded malformed optional data and version-inapplicable attributes.
 * @ingroup model
 */
enum class ParseMode {
  Strict, ///< Reject malformed/inapplicable optional attributes and trailing bytes.
  Compatible, ///< Recover bounded optional damage with diagnostics while rejecting unsafe essential structure.
};

/**
 * @brief Per-read resource ceilings. Byte budgets include retained source and model storage; they
 * do not impose a permanent budget on later model edits.
 * @ingroup model
 */
struct ReadLimits {
  std::size_t max_input_bytes = std::size_t{64} * 1024 * 1024; ///< Maximum complete class-file input size in bytes.
  std::size_t max_total_allocated_bytes = std::size_t{256} * 1024 * 1024; ///< Maximum charged source/model allocation bytes during parsing.
  std::size_t max_attributes = 100'000; ///< Maximum attribute envelopes visited in one class.
  std::size_t max_annotation_depth = 128; ///< Maximum nested annotation/array element depth.
  std::size_t max_switch_cases = MAX_CODE_SIZE / 4; ///< Maximum switch case entries accepted per switch.
  std::size_t max_methods = 65535; ///< Maximum method declarations in one class.
  std::size_t max_code_bytes = MAX_CODE_SIZE; ///< Maximum code-array byte length per method.
  std::size_t max_diagnostics = 1024; ///< Maximum retained diagnostics before a suppression diagnostic is emitted.
};

/**
 * @brief Class parsing and recovery policy.
 *
 * Compatible recovery is the default; opaque essential Code remains opt-in.
 * @ingroup model
 */
struct ReadOptions {
  ParseMode mode = ParseMode::Compatible; ///< Strict rejection or compatible recovery policy.
  ReadLimits limits = {}; ///< Configured resource ceilings.
  bool allow_opaque_code = false; ///< Permit retaining bounded undecodable Code in compatible mode.
  /**
   * @brief Borrowed allocator that must outlive the returned ClassFile; null selects the default resource.
   */
  std::pmr::memory_resource* upstream_resource = std::pmr::get_default_resource();
};

} // namespace cafe
