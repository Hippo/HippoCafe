/**
 * @file
 * @brief Transactional pool compaction for symbolic edits that run out of slots.
 * @ingroup implementation
 */

#pragma once

#include <functional>
#include <span>

#include <cafe/ids.hpp>

namespace cafe::detail {

struct ClassStorage;

/// Retry callback that receives a map from original to compacted pool indices.
using CompactedPoolAttempt = std::function<bool(std::span<const CpIndex>)>;

/// Try the edit once against a compacted pool when compaction frees slots.
/// @param storage Class whose constant references need updating.
/// @param roots Additional pool indices used by the pending edit.
/// @param attempt Invoked with the old-to-new index map. Returning false or throwing restores the previous pool.
void retry_with_compact_pool(ClassStorage& storage, std::span<const CpIndex> roots,
                             const CompactedPoolAttempt& attempt);

} // namespace cafe::detail
