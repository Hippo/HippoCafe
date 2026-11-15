/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>

#include "cafe/bytecode/opcode.hpp"
#include "cafe/class_version.hpp"
#include "cafe/error.hpp"
#include "cafe/ids.hpp"

namespace cafe {

class ConstantPool;
class MethodCode;

namespace detail {

/**
 * @brief Scratch-owned wire opcode choices, instruction offsets, label offsets, and
 * conditional-expansion decisions after layout convergence.
 * @ingroup implementation
 */
struct CodeLayout {
  /**
   * @brief Initialize scratch-owned wire opcode choices, instruction offsets, label offsets, and
   * conditional-expansion decisions after layout convergence.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   */
  explicit CodeLayout(std::pmr::memory_resource* scratch) :
      encodings(scratch), offsets(scratch), label_offsets(scratch), expanded_conditionals(scratch) {
  }

  std::pmr::vector<Opcode> encodings; ///< Chosen wire opcode for each logical instruction.
  /**
   * @brief Final byte offset for each instruction, followed by the exclusive code end.
   */
  std::pmr::vector<std::uint32_t> offsets;
  std::pmr::vector<std::uint32_t> label_offsets; ///< Resolved byte offsets indexed by method-local label value.
  std::pmr::vector<std::uint8_t> expanded_conditionals; ///< Flags marking conditional branches emitted through a long-branch expansion.
  std::uint32_t scope = 0; ///< Method identity used to reject foreign labels or switches.

  /**
   * @brief Resolve a bound scoped label after layout convergence.
   * @param id Label handle scoped to the supplied method.
   * @return The final byte offset, or nullopt for a foreign/unbound label.
   */
  [[nodiscard]] std::optional<std::uint32_t> offset_of(LabelId id) const;
  /**
   * @brief Check whether a conditional at a method-order index was expanded.
   * @param index Zero-based index in the corresponding table or slot array.
   * @return True for a synthetic inverse-conditional plus goto_w emission.
   */
  [[nodiscard]] bool expanded(std::size_t index) const {
    return !expanded_conditionals.empty() && expanded_conditionals[index] != 0;
  }
};

/**
 * @brief Choose encodings and converge branch widths and switch alignment before emission.
 * @param code Decoded instruction sequence or associated Code attribute.
 * @param version Exact JVM class-file major and minor version.
 * @param pool Owning class constant pool used for reference validation.
 * @param max_locals Maximum local-variable size in JVM slots.
 * @param preserve_original_encodings Whether to honor retained wire-encoding hints when laying out
 *   instructions.
 * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
 * @param path Borrowed structural path included in diagnostics.
 * @param expand_conditionals Whether long conditional branches may become inverse branches
 *   followed by goto_w.
 * @return A scratch-owned layout, or an invalid operand/label/size/convergence error.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<CodeLayout, WriteError> layout_code(
    const MethodCode& code, ClassVersion version, const ConstantPool& pool,
    std::uint16_t max_locals, bool preserve_original_encodings,
    std::pmr::memory_resource* scratch, std::string_view path, bool expand_conditionals = false);

} // namespace detail
} // namespace cafe
