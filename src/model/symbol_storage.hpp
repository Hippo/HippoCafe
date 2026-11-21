/**
 * @file
 * @brief Stable backing for deferred symbolic bytecode operands.
 * @ingroup implementation
 */
#pragma once

#include <cafe/bytecode/method_instruction.hpp>
#include <list>
#include <memory_resource>
#include <optional>
#include <stop_token>
#include <string>

namespace cafe::detail {

/// Owns names and potentially shared or cyclic bootstrap argument arrays.
struct SymbolStorage {
  /// Bind names and argument buffers to the given resource.
  /// @param resource Allocator resource that outlives the stored operands.
  explicit SymbolStorage(std::pmr::memory_resource* resource) : names(resource), arguments(resource) {
  }

  /// Copy text and bootstrap data, visiting each reachable argument array once.
  /// @param value Borrowed source instruction.
  /// @return Instruction whose symbolic operands are backed by this storage.
  MethodInstruction copy(const MethodInstruction& value);

  /**
   * @brief Copy symbolic text and bootstrap data without recursing through the caller's graph.
   * @param value Source instruction. Raw instruction, branch and switch values are passed through.
   * @param max_work Maximum number of reachable bootstrap argument entries to copy.
   * @param cancellation Optional cooperative stop token.
   * @return A copy backed by this storage, or nullopt on cancellation. A work limit throws BudgetExceeded.
   */
  std::optional<MethodInstruction> copy(const MethodInstruction& value,
                                        std::size_t max_work,
                                        std::stop_token cancellation);

  std::pmr::list<std::pmr::string> names; ///< Stable backing for symbolic names.
  std::pmr::list<std::pmr::vector<BootstrapArgument>> arguments; ///< Stable owned bootstrap argument arrays.
};

} // namespace cafe::detail
