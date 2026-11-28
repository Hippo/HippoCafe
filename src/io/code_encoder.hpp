/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <span>

#include "code_layout.hpp"

namespace cafe::detail {

/**
 * @brief Scratch-owned emitted bytes and final layout maps. Its resource must outlive all returned
 * spans and subsequent metadata emission.
 * @ingroup implementation
 */
class EncodedCode {
public:
  /**
   * @brief Borrow the completed encoded code array.
   * @return A scratch-owned byte span.
   */
  [[nodiscard]] std::span<const std::byte> bytes() const {
    return output;
  }
  /**
   * @brief Borrow the encoded instruction-start bitmap.
   * @return A scratch-owned bitmap indexed by byte offset.
   */
  [[nodiscard]] std::span<const std::uint8_t> instruction_starts() const {
    return starts;
  }
  /**
   * @brief Borrow final instruction offsets in method order.
   * @return A scratch-owned offset span including the exclusive end.
   */
  [[nodiscard]] std::span<const std::uint32_t> instruction_offsets() const {
    return layout.offsets;
  }
  /**
   * @brief Resolve a scoped label into its final byte offset.
   * @param id Label handle scoped to the supplied method.
   * @return The byte offset, or nullopt for an invalid/unbound label.
   */
  [[nodiscard]] std::optional<std::uint32_t> offset_of(LabelId id) const {
    return layout.offset_of(id);
  }
  /**
   * @brief Check whether layout expanded a conditional into an inverse branch and goto_w.
   * @return True if any expansion introduced an additional frame boundary.
   */
  [[nodiscard]] bool has_expanded_conditionals() const {
    return !layout.expanded_conditionals.empty();
  }

  /// Compare with a retained code array that may use pre-compaction indices.
  /// @param source Original bytecode array.
  /// @param code Method corresponding to this encoding.
  /// @param pool_remapped Whether to ignore constant operand bytes.
  /// @return Whether the encodings match outside any remapped operands.
  [[nodiscard]] bool matches_source(std::span<const std::byte> source, const MethodCode& code, bool pool_remapped) const;

private:
  /**
   * @brief Initialize scratch-owned emitted bytes and final layout maps
   * @param layout Completed instruction layout transferred into encoded output.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   */
  explicit EncodedCode(CodeLayout layout, std::pmr::memory_resource* scratch);

  // The namespace-level declaration below owns the contract. Extracting this duplicate
  // friend declaration makes Doxygen generate a member link without a matching anchor.
  /** @cond duplicate_friend_declarations */
  friend std::expected<EncodedCode, WriteError> encode_code(
      const MethodCode&, ClassVersion, const ConstantPool&, std::uint16_t, bool,
      std::pmr::memory_resource*, std::string_view, bool);
  /** @endcond */

  CodeLayout layout; ///< Final instruction encoding, offset, and conditional-expansion decisions.
  std::pmr::vector<std::byte> output; ///< Owned encoded code-array bytes allocated through the supplied scratch resource.
  std::pmr::vector<std::uint8_t> starts; ///< Byte-indexed bitmap of opcode boundaries.
};

/**
 * @brief Validate, lay out, and encode logical instructions into scratch storage.
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
 * @return Encoded bytes and offset maps, or a structured emission error.
 * @ingroup implementation
 *
 * The returned arrays use scratch. Keep the resource alive through any
 * exception-table and metadata emission that uses their resolved offsets.
 */
[[nodiscard]] std::expected<EncodedCode, WriteError> encode_code(
    const MethodCode& code, ClassVersion version, const ConstantPool& pool,
    std::uint16_t max_locals, bool preserve_original_encodings,
    std::pmr::memory_resource* scratch, std::string_view path, bool expand_conditionals = false);

} // namespace cafe::detail
