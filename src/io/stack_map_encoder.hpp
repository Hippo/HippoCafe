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
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cafe/error.hpp>
#include <cafe/ids.hpp>

namespace cafe {
class ConstantPool;
class MethodCode;

namespace analysis {
class FrameAnalysis;
}
} // namespace cafe

namespace cafe::detail {

/**
 * @brief Writer-local append-only extension of the original constant pool for frame reference
 * types. Existing indices remain unchanged and the canonical pool is never mutated.
 * @ingroup implementation
 *
 * The frame encoder stages new constants beside the original pool. Existing
 * references, including every ldc operand, retain their original indices.
 */
class StackMapConstants {
public:
  /**
   * @brief Initialize writer-local append-only extension of the original constant pool for frame
   * reference types
   * @param pool Owning class constant pool used for reference validation.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  StackMapConstants(const ConstantPool& pool, std::pmr::memory_resource* resource);

  /**
   * @brief Borrow encoded constant-pool extension bytes.
   * @return A view valid until staging mutation or destruction.
   */
  [[nodiscard]] std::span<const std::byte> bytes() const {
    return extra;
  }
  /**
   * @brief Read the combined original and staged constant-pool count.
   * @return The u2-compatible slot count.
   */
  [[nodiscard]] std::uint16_t count() const;
  /**
   * @brief Reuse or stage a UTF-8 constant needed by stack-map emission.
   * @param text Borrowed UTF-8 text to compare, validate, or encode.
   * @param path Borrowed structural path included in diagnostics.
   * @return Its stable output index, or a pool/size error.
   */
  [[nodiscard]] std::expected<CpIndex, WriteError> utf8(std::string_view text, std::string_view path);
  /**
   * @brief Reuse or stage a Class constant and its UTF-8 name.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param path Borrowed structural path included in diagnostics.
   * @return Its stable output index, or a pool/size error.
   */
  [[nodiscard]] std::expected<CpIndex, WriteError> class_reference(std::string_view name, std::string_view path);

private:
  /**
   * @brief Index reusable class and UTF-8 entries without changing the original pool.
   */
  void index_original_pool();

  const ConstantPool& pool; ///< Borrowed owning class constant pool used for lookup and validation.
  std::pmr::monotonic_buffer_resource names; ///< Monotonic backing storage for stable writer-local reference-name views.
  std::pmr::unordered_map<std::string_view, CpIndex> utf8_indices; ///< Reusable UTF-8 constant indices keyed by encoded text.
  std::pmr::unordered_map<std::string_view, CpIndex> class_indices; ///< Reusable Class constant indices keyed by internal name.
  std::pmr::vector<std::byte> extra; ///< Encoded writer-local constant-pool extension bytes.
  std::size_t next_index; ///< Next unused output constant-pool slot.
  bool indexed = false; ///< Whether original constant-pool reuse tables have been populated.
};

/**
 * @brief Scratch-owned mapping from stable instruction identity to final byte offset.
 * @ingroup implementation
 */
class InstructionOffsets {
public:
  /**
   * @brief Initialize scratch-owned mapping from stable instruction identity to final byte offset.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param offsets Instruction-order byte offsets for the selected code layout.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   */
  InstructionOffsets(const MethodCode& code, std::span<const std::uint32_t> offsets,
                     std::pmr::memory_resource* scratch);

  /**
   * @brief Resolve an instruction handle to its final emitted byte offset.
   * @param id Stable instruction handle belonging to the supplied method.
   * @return The offset, or nullopt for a stale/foreign/unmapped handle.
   */
  [[nodiscard]] std::optional<std::uint16_t> offset_of(InstructionId id) const;

private:
  /**
   * @brief Instruction generation and final byte offset for a handle slot.
   * @ingroup implementation
   */
  struct Entry {
    InstructionId instruction; ///< Instruction identity associated with this record.
    std::uint16_t offset; ///< Byte offset relative to the owning source or code buffer.
  };
  std::pmr::vector<Entry> entries; ///< Slot-indexed instruction generation and final-byte-offset lookup.
};

/**
 * @brief Encode compact StackMapTable frames against final instruction offsets and staged
 * constants.
 * @param analysis Completed verifier result whose frames are emitted.
 * @param code Decoded instruction sequence or associated Code attribute.
 * @param offsets Final instruction-identity to encoded byte-offset lookup.
 * @param constants Writer-local constant-pool extension used by frame encoding.
 * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
 * @param path Borrowed structural path included in diagnostics.
 * @return Scratch-owned bytes, or an invalid/unrepresentable frame error.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<std::pmr::vector<std::byte>, WriteError> encode_stack_map(
    const analysis::FrameAnalysis& analysis, const MethodCode& code, const InstructionOffsets& offsets,
    StackMapConstants& constants, std::pmr::memory_resource* scratch, std::string_view path);

} // namespace cafe::detail
