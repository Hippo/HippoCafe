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

#include <cafe/error.hpp>

#include "attribute_registry.hpp"

namespace cafe {

class ConstantPool;
struct ReadLimits;

namespace detail {

class BinaryReader;

/**
 * @brief Code length, instruction-boundary bitmap, exception count, and local limit used to
 * validate nested raw attributes.
 * @ingroup implementation
 */
struct CodeAttributeContext {
  std::span<const std::byte> bytes; ///< Borrowed bounded code-array bytes, excluding the Code envelope.
  /**
   * @brief Borrowed byte-indexed bitmap identifying opcode boundaries.
   *
   * Indexed by byte offset into bytes. Nonzero entries mark opcode boundaries.
   */
  std::span<const std::uint8_t> instruction_starts;
  std::uint16_t max_locals; ///< Declared local-slot limit used for operand and metadata validation.
  std::uint16_t exception_count; ///< Number of exception-table entries available to handler annotations.
};

/**
 * @brief Location and semantic role of a u2 Code offset/range/handler ordinal within an opaque
 * attribute body.
 * @ingroup implementation
 */
struct CodeOffsetField {
  /**
   * @brief Semantic role of a u2 offset-bearing Code attribute field.
   * @ingroup implementation
   */
  enum class Kind {
    Line, ///< Line-number instruction offset.
    Range, ///< Start offset and byte-length interval.
    InstructionAnnotation, ///< Instruction offset in a type-annotation target.
    Handler, ///< Exception-table ordinal independent of byte offsets.
  };
  Kind kind; ///< Discriminator selecting the interpretation of this record.
  std::size_t position; ///< Byte position of the offset-bearing u2 field within the bounded attribute body.
  std::uint16_t value; ///< Original instruction offset or exception-table ordinal selected by kind.
  std::uint16_t length = 0; ///< Length of the byte range.
};

/**
 * @brief Optional callback for every validated offset-bearing field in a recognized Code
 * attribute. Callback state is borrowed for the validation call.
 * @ingroup implementation
 */
struct CodeOffsetVisitor {
  void* state = nullptr; ///< Borrowed opaque callback state valid for the validation call.
  bool (*visit)(void*, CodeOffsetField&) = nullptr; ///< Borrowed offset visitor callback; false rejects relocation/validation.
  void (*type_annotation)(void*, std::size_t, std::size_t) = nullptr; ///< Receives the bounded byte interval of each validated type annotation.
  void (*local_slot)(void*, std::uint16_t, std::uint16_t) = nullptr; ///< Receives each referenced local slot and its descriptor width.
};

/**
 * @brief Validate a bounded recognized opaque body and optionally visit code-offset fields.
 * @param body Length-bounded attribute body reader.
 * @param spec Registry rules for the recognized attribute.
 * @param context Structural attribute location checked against the format registry.
 * @param cp Owning class constant pool.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param path Borrowed structural path included in diagnostics.
 * @param code Optional bounded Code bytes, instruction boundaries, local limit, and handler-count
 *   context.
 * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
 * @param offsets Optional callback for inspecting offset-bearing fields during validation.
 * @return Success or a structural read error; the original bytes remain the retained payload.
 * @ingroup implementation
 *
 * The caller gates version and context before validating the bounded body.
 * Validated attributes still retain their complete original bytes.
 */
[[nodiscard]] std::expected<void, ReadError> validate_opaque_attribute(
    BinaryReader& body, const AttributeSpec& spec, AttributeContext context,
    const ConstantPool& cp, const ReadLimits& limits, std::string_view path,
    const CodeAttributeContext* code = nullptr,
    std::pmr::memory_resource* scratch = std::pmr::get_default_resource(),
    CodeOffsetVisitor offsets = {});

} // namespace detail
} // namespace cafe
