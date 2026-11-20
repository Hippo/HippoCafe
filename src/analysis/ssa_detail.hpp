/**
 * @file
 * @brief Shared graph storage for SSA construction, verification and destruction.
 * @ingroup implementation
 */
#pragma once
#include <cafe/analysis/ssa.hpp>
#include <list>
#include <string>
#include <unordered_set>
#include "../model/budget_resource.hpp"
#include "ssa_commit.hpp"

namespace cafe::analysis {

/// Hash graph-owned names and borrowed reference strings without allocating during lookup.
struct SsaNameHash {
  using is_transparent = void; ///< Enable heterogeneous lookup with string views.
  /**
   * @brief Hash the contents of a borrowed name.
   * @param name Internal JVM class name or descriptor.
   * @return Hash suitable for the intern table.
   */
  std::size_t operator()(std::string_view name) const noexcept {
    return std::hash<std::string_view>{}(name);
  }
};

/// Per-operation backing for a possibly cyclic symbolic bootstrap argument graph.
struct SsaSymbols {
  /**
   * @brief Bind symbolic operand storage to the graph budget.
   * @param resource Resource outliving this operand.
   */
  explicit SsaSymbols(std::pmr::memory_resource* resource) : names(resource), arguments(resource) {
  }
  SsaOperationId owner{}; ///< Operation retaining this backing storage.
  std::pmr::list<std::pmr::string> names; ///< Stable backing for symbolic names.
  std::pmr::list<std::pmr::vector<BootstrapArgument>> arguments; ///< Stable owned bootstrap argument arrays.
};

/// Graph storage. Handle slots are append-only, and a cleared id permanently retires a slot.
struct SsaStorage {
  /**
   * @brief Create a graph with a fresh identity and copied policy.
   * @param method Class-relative method index.
   * @param limits Graph budgets and allocator.
   * @param stamp Bound source method and hierarchy identities.
   */
  SsaStorage(std::size_t method, const AnalysisLimits& limits, cafe::detail::SsaSourceStamp stamp);

  AnalysisLimits limits; ///< Limits and borrowed allocator copied from the context.
  cafe::detail::BudgetResource budget; ///< Counts live graph allocations.
  std::size_t live_operations = 0; ///< Live operations counted against the graph limit.
  std::size_t live_edges = 0; ///< Live transfers counted against the graph limit.
  std::uint64_t scope; ///< Generation shared by all handles into this graph.
  std::size_t method_index; ///< Bound method index.
  cafe::detail::SsaSourceStamp source_stamp; ///< Snapshot checked before graph writeback.
  std::pmr::unordered_set<std::pmr::string, SsaNameHash, std::equal_to<>> names; ///< Interned SSA type names.
  std::pmr::vector<SsaBlock> blocks; ///< Blocks in emission order.
  std::pmr::vector<SsaOperation> operations; ///< Operation slots indexed by scoped handle.
  std::pmr::vector<SsaValue> values; ///< Definition slots indexed by scoped handle.
  std::pmr::vector<SsaEdge> edges; ///< Transfer slots indexed by scoped handle.
  std::pmr::vector<SsaHandler> handlers; ///< Clauses in original catch priority order.
  std::pmr::vector<SsaValueId> parameters; ///< Receiver and arguments in descriptor order.
  std::pmr::list<SsaSymbols> symbols; ///< Storage retained for live symbolic operations.

  /**
   * @brief Intern a name in stable graph-owned storage.
   * @param name Borrowed name.
   * @return Name view valid through graph destruction.
   */
  std::string_view own(std::string_view name);
  /**
   * @brief Intern reference-type text.
   * @param type Type with a potentially borrowed name.
   * @return Type using graph-owned name storage.
   */
  SsaType own(SsaType type);
  /**
   * @brief Append a fresh typed definition.
   * @param kind Source of the definition.
   * @param block Block containing or receiving the definition.
   * @param type Verifier category and allocation identity.
   * @return Fresh value handle. Budget exhaustion throws BudgetExceeded.
   */
  SsaValueId add_value(SsaValueKind kind, SsaBlockId block, SsaType type);
};

/// Narrow implementation access shared by the verifier and lowering pipeline.
struct SsaAccess {
  /**
   * @brief Borrow mutable graph internals.
   * @param graph A live graph.
   * @return Graph-owned backing store.
   */
  static SsaStorage& get(SsaMethod& graph) {
    return *graph.storage;
  }
  /**
   * @brief Borrow graph internals for validation or emission.
   * @param graph A live graph.
   * @return Read-only graph-owned backing store.
   */
  static const SsaStorage& get(const SsaMethod& graph) {
    return *graph.storage;
  }
  /**
   * @brief Wrap a fresh store in the public graph owner.
   * @param storage Detached store of the new graph.
   * @return Independent movable graph.
   */
  static SsaMethod make(std::unique_ptr<SsaStorage> storage) {
    return SsaMethod(std::move(storage));
  }
};
} // namespace cafe::analysis
