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
  LabelId local_begin{}; ///< Includes synthetic instructions inserted before this operation in its block.
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
  std::size_t debug_local_count = 0; ///< Required slot count for mapped variables, including unused category-2 entries.
};

/**
 * @brief Collect the widths of slots referenced by local-variable ranges in Code metadata.
 * @param owner Owner of the constant pool and source bytes.
 * @param source Source method and original offset anchors.
 * @param limits Work and cancellation limits for inspecting metadata.
 * @param remaining Remaining work charged for inspected bytes.
 * @param scratch Budgeted allocator for the returned slot map and parser scratch.
 * @param anchors Optional output bitmap of source instructions anchoring Code metadata.
 * @return Zero for invisible slots, or one or two for each referenced slot.
 */
[[nodiscard]] std::expected<std::pmr::vector<std::uint8_t>, WriteError> ssa_debug_slot_widths(
    const ClassFile& owner, const CodeAttribute& source, const analysis::AnalysisLimits& limits,
    std::size_t& remaining, std::pmr::memory_resource* scratch, std::pmr::vector<std::uint8_t>* anchors = nullptr);

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
