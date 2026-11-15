/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

#include "cafe/bytecode/code.hpp"
#include "cafe/class_version.hpp"
#include "cafe/error.hpp"
#include "cafe/read_options.hpp"

namespace cafe {

class ConstantPool;

namespace detail {

class CodeDecoder;

/**
 * @brief Scratch-owned original instruction-start bitmap, instruction ordinals, and scoped labels.
 * Metadata boundaries are counted separately for offset remapping.
 * @ingroup implementation
 */
class DecodedBoundaries {
public:
  /**
   * @brief Check whether an original code offset begins an instruction.
   * @param offset Byte offset into the supplied source/output range.
   * @return True only for a real opcode start before the exclusive end.
   */
  [[nodiscard]] bool is_instruction_start(std::size_t offset) const;
  /**
   * @brief Check whether an offset is an instruction boundary or the exclusive end.
   * @param offset Byte offset into the supplied source/output range.
   * @return True for a legal boundary.
   */
  [[nodiscard]] bool is_boundary(std::size_t offset) const;
  /**
   * @brief Borrow the original byte-indexed instruction-start bitmap.
   * @return A span valid while scratch-backed boundary storage remains alive.
   */
  [[nodiscard]] std::span<const std::uint8_t> instruction_starts() const {
    return starts;
  }
  /**
   * @brief Reuse or bind a scoped label at an original boundary.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param offset Byte offset into the supplied source/output range.
   * @return The label, or an invalid boundary/handle-limit error.
   */
  [[nodiscard]] std::expected<LabelId, CodeEditError> label_at(MethodCode& code, std::size_t offset);
  /**
   * @brief Mark a validated original instruction offset needed for later metadata relocation.
   * @param offset Byte offset into the supplied source/output range.
   */
  void retain_metadata_boundary(std::size_t offset);
  /**
   * @brief Count distinct retained metadata instruction boundaries.
   * @return The number of marked boundaries, excluding the exclusive end.
   */
  [[nodiscard]] std::size_t metadata_boundary_count() const {
    return metadata_boundaries;
  }
  /**
   * @brief Resolve a marked original offset to its instruction-order position.
   * @param offset Byte offset into the supplied source/output range.
   * @return The ordinal, or nullopt for an unmarked/non-instruction offset.
   */
  [[nodiscard]] std::optional<std::size_t> metadata_ordinal(std::size_t offset) const;

private:
  friend class CodeDecoder;
  /**
   * @brief Initialize scratch-owned original instruction-start bitmap, instruction ordinals, and
   * scoped labels
   * @param length Number of bytes in the bounded range or encoded payload.
   * @param scope Method identity used for scoped labels and switches.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   */
  explicit DecodedBoundaries(std::size_t length, std::uint32_t scope, std::pmr::memory_resource* scratch);

  std::pmr::vector<std::uint8_t> starts; ///< Byte-indexed bitmap of opcode boundaries.
  std::pmr::vector<std::uint16_t> ordinals; ///< Instruction-order ordinals indexed by original byte offset.
  std::pmr::vector<LabelId> labels; ///< Method-scoped labels indexed by original code byte offset.
  std::uint32_t scope; ///< Method identity used to reject foreign labels or switches.
  std::size_t metadata_boundaries = 0; ///< Number of distinct original instruction offsets retained for metadata relocation.
};

/**
 * @brief Decode a bounded code array into an empty temporary instruction owner.
 * @param code Decoded instruction sequence or associated Code attribute.
 * @param bytes Code byte sequence borrowed only for this call.
 * @param absolute_offset Absolute source byte offset of the bounded code array.
 * @param version Exact JVM class-file major and minor version.
 * @param pool Owning class constant pool used for reference validation.
 * @param max_locals Maximum local-variable size in JVM slots.
 * @param options Operation-specific parsing, emission, or entry policy.
 * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
 * @param path Borrowed structural path included in diagnostics.
 * @return Scratch-owned original boundaries, or a structured read failure; code storage remains
 *   method-owned.
 * @ingroup implementation
 *
 * Decode into an empty temporary MethodCode. The returned boundary table
 * borrows its allocator from scratch; code storage remains owned by the method.
 */
[[nodiscard]] std::expected<DecodedBoundaries, ReadError> decode_code(
    MethodCode& code, std::span<const std::byte> bytes, std::size_t absolute_offset,
    ClassVersion version, const ConstantPool& pool, std::uint16_t max_locals,
    const ReadOptions& options, std::pmr::memory_resource* scratch, std::string_view path);

} // namespace detail
} // namespace cafe
