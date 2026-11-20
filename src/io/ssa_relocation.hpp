/**
 * @file
 * @brief Relocate Code metadata using the bytecode provenance of SSA destruction.
 * @ingroup implementation
 */
#pragma once

#include <expected>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>

#include <cafe/analysis/limits.hpp>
#include <cafe/attribute.hpp>
#include <cafe/error.hpp>

namespace cafe::detail {

class EncodedCode;
struct CodeAttributeContext;

/// Lowered bytecode interval for one source instruction. An exact instruction can carry type annotations.
struct SsaInstructionOrigin {
  InstructionId source; ///< Original method instruction.
  LabelId begin; ///< Inclusive start in the staged method.
  LabelId end; ///< Exclusive end in the staged method.
  std::optional<InstructionId> exact; ///< Staged instruction implementing the original operation.
};

/// Live interval during which a physical slot contains the source local's value.
struct SsaLocalOrigin {
  std::uint16_t source_slot; ///< Original JVM local slot.
  std::uint16_t target_slot; ///< Lowered JVM local slot.
  LabelId begin; ///< Inclusive staged boundary.
  LabelId end; ///< Exclusive staged boundary.
};

/// Method-scoped provenance recorded during SSA bytecode emission.
struct SsaCodeProvenance {
  /// Create empty provenance tables with the default memory resource.
  SsaCodeProvenance() = default;
  /**
   * @brief Keep provenance tables within the lowering operation's memory budget.
   * @param resource Allocator that outlives these tables.
   */
  explicit SsaCodeProvenance(std::pmr::memory_resource* resource) : instructions(resource), locals(resource), handler_sources(resource) {
  }

  std::pmr::vector<SsaInstructionOrigin> instructions; ///< Source instructions and their emitted intervals.
  std::pmr::vector<SsaLocalOrigin> locals; ///< Lowered locations for original local values.
  std::pmr::vector<std::optional<std::uint16_t>> handler_sources; ///< Original clause of each emitted handler row.
  bool preserves_local_slots = false; ///< All source local writes and homes are maintained during their debug lifetimes.
};

/**
 * @brief Determine whether SSA must preserve local slots and assignments for source metadata.
 * @param owner Class owning the constant-pool names of nested attributes.
 * @param code Code attribute being lifted or lowered.
 * @return Whether a local-variable or Code type-annotation table may refer to original local slots.
 */
[[nodiscard]] bool ssa_has_local_metadata(const ClassFile& owner, const CodeAttribute& code);

/**
 * @brief Build a new ordered Code attribute list for one staged SSA body.
 * @param owner Class owning the source and destination constant pool.
 * @param source Current Code and its original or revision-stamped metadata.
 * @param staged Detached instructions before final branch widening.
 * @param encoded Final layout of the staged instructions.
 * @param provenance Source instruction, local, and handler identities supplied by lowering.
 * @param output_context Final limits, boundaries and handler count used for independent validation.
 * @param drop_unsafe Permit dropping an unrelocatable attribute with a diagnostic.
 * @param diagnostics Append one warning per attribute deliberately dropped.
 * @param scratch Budgeted scratch storage for the duration of this call.
 * @param path Structural path to the method Code.
 * @param limits Bound work, output allocation and cancellation.
 * @param stack_map Optional replacement StackMapTable transferred into the resulting list.
 * @return Replacement attributes or a structural, relocation, or budget failure.
 */
[[nodiscard]] std::expected<std::vector<Attribute>, WriteError> relocate_ssa_attributes(
    const ClassFile& owner, const CodeAttribute& source, const MethodCode& staged, const EncodedCode& encoded,
    const SsaCodeProvenance& provenance, const CodeAttributeContext& output_context, bool drop_unsafe,
    std::vector<Diagnostic>& diagnostics, std::pmr::memory_resource* scratch, std::string_view path,
    const analysis::AnalysisLimits& limits, Attribute* stack_map = nullptr);

} // namespace cafe::detail
