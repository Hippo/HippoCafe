/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#pragma once

#include <unordered_map>

#include "analysis_detail.hpp"
#include "cafe/classpath/class_hierarchy.hpp"

namespace cafe::analysis {

/**
 * @brief Frame position identified by block entry, slot index, and local-versus-stack location.
 * @ingroup implementation
 */
struct JoinSlot {
  InstructionId block_entry; ///< Stable instruction identity at the beginning of the destination block.
  std::size_t index; ///< Zero-based local-slot or operand-value position within the entry frame.
  bool local; ///< True for a local slot, false for an operand-stack value.

  friend bool operator==(JoinSlot, JoinSlot) = default;
};

/**
 * @brief Bounded deterministic alternative selection for ambiguous reference joins. Candidate
 * choices replay across worklist attempts; interned names remain stable for the search lifetime.
 * @ingroup implementation
 */
class JoinSearch {
public:
  /**
   * @brief Initialize bounded deterministic alternative selection for ambiguous reference joins
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  explicit JoinSearch(std::pmr::memory_resource* resource) :
      names(resource), indices(resource), choices(resource) {
  }

  /**
   * @brief Select the deterministic candidate for this frame-slot/reference-pair join during the
   * current attempt.
   * @param slot Frame-local join position identifying a deterministic candidate choice.
   * @param left First input verification type or reference name.
   * @param right Second input verification type or reference name.
   * @param common Hierarchy join result with fallback type and candidate alternatives.
   * @return A stable interned reference-name view.
   */
  [[nodiscard]] std::string_view select(JoinSlot slot, std::string_view left, std::string_view right,
                                        const classpath::JoinResult& common);
  /**
   * @brief Check whether any join recorded alternative reference candidates.
   * @return True when an alternative-choice sequence exists.
   */
  [[nodiscard]] bool encountered_ambiguity() const noexcept {
    return !choices.empty();
  }
  /**
   * @brief Advance the latest pending alternative and discard dependent later choices.
   * @return True if another deterministic attempt is available, false when choices are exhausted.
   */
  [[nodiscard]] bool advance();

private:
  /**
   * @brief Frame-join slot plus ordered reference names identifying one candidate choice.
   * @ingroup implementation
   */
  struct Key {
    JoinSlot slot; ///< Frame join position identifying one deterministic candidate choice.
    std::string_view left; ///< First reference name in a join/cache pair.
    std::string_view right; ///< Second reference name in a join/cache pair.

    friend bool operator==(const Key&, const Key&) = default;
  };

  /**
   * @brief Hash for a frame-join slot and reference-pair key.
   * @ingroup implementation
   */
  struct KeyHash {
    /**
     * @brief Hash a frame slot identity and its reference pair.
     * @param key Frame-slot and reference-pair key to hash.
     * @return The cache-key hash.
     */
    [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
  };

  /**
   * @brief Selected alternative and alternative count for one ambiguous frame-join key.
   * @ingroup implementation
   */
  struct Choice {
    Key key; ///< Frame slot and reference pair identifying this alternative choice.
    /**
     * @brief Selected ambiguous-join alternative; zero is the fallback superclass.
     *
     * Zero is the common superclass. Alternatives are one-based.
     */
    std::size_t selected;
    std::size_t alternatives; ///< Number of alternate reference candidates available for this join.
  };

  NameSet names; ///< Stable owned reference-name intern set.
  std::pmr::unordered_map<Key, std::size_t, KeyHash> indices; ///< Lookup from an ambiguous join key to its stored choice index.
  std::pmr::vector<Choice> choices; ///< Deterministic ambiguous-reference choices in encounter order.
};

/**
 * @brief Verifier-state merging and assignability checks, with optional hierarchy-backed ambiguous
 * joins and scratch-owned reference names.
 * @ingroup implementation
 */
class FrameMerger {
public:
  /**
   * @brief Initialize verifier-state merging and assignability checks, with optional
   * hierarchy-backed ambiguous joins and scratch-owned reference names.
   * @param hierarchy Reference-resolution hierarchy; a null internal pointer selects category-only
   *   analysis.
   * @param search Deterministic reference-join candidate selection state shared across attempts.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   * @param verifier_interfaces Enable JVM interface assignability when checking declared states.
   */
  FrameMerger(const classpath::ClassHierarchy* hierarchy, JoinSearch& search,
              std::pmr::memory_resource* scratch, bool verifier_interfaces = false) :
      hierarchy(hierarchy), search(search), names(scratch), verifier_interfaces(verifier_interfaces) {
  }

  /**
   * @brief Join incoming verifier state into a block entry while enforcing stack shape and
   * constructor state.
   * @param target Method-scoped branch destination.
   * @param incoming Incoming predecessor verification frame.
   * @param block_entry Stable instruction handle belonging to the supplied method.
   * @return Whether the target changed, or a structured incompatibility/resolution error.
   */
  [[nodiscard]] std::expected<bool, AnalysisError> merge(Frame& target, const Frame& incoming,
                                                         InstructionId block_entry);
  /**
   * @brief Check that an incoming transition is assignable to the converged declared frame.
   * @param target Method-scoped branch destination.
   * @param incoming Incoming predecessor verification frame.
   * @return Success or an incompatibility/type/resolution error.
   */
  [[nodiscard]] std::expected<void, AnalysisError> check(const Frame& target, const Frame& incoming) const;

private:
  /**
   * @brief Merge two slot types, consulting deterministic reference candidates when needed.
   * @param left First input verification type or reference name.
   * @param right Second input verification type or reference name.
   * @param slot Frame-local join position identifying a deterministic candidate choice.
   * @return The merged verification type, or an incompatible/uninitialized/resolution error.
   */
  [[nodiscard]] std::expected<VerificationType, AnalysisError> join(
      VerificationType left, VerificationType right, JoinSlot slot);
  /**
   * @brief Check frame-type assignability, including Top and uninitialized identities.
   * @param from Graph-local block whose outgoing edges are requested.
   * @param to Destination verification type.
   * @return True if assignable, false if incompatible, or a hierarchy-resolution failure.
   */
  [[nodiscard]] std::expected<bool, AnalysisError> assignable(
      VerificationType from, VerificationType to) const;

  const classpath::ClassHierarchy* hierarchy; ///< Optional borrowed reference hierarchy; null selects category-only checking.
  JoinSearch& search; ///< Shared deterministic candidate-selection state across verifier attempts.
  NameSet names; ///< Stable owned reference-name intern set.
  bool verifier_interfaces; ///< Apply JVM interface assignment when checking an explicit frame.
};

} // namespace cafe::analysis
