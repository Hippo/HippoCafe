/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <cafe/io/class_reader.hpp>

#include "../model/class_storage.hpp"
#include "binary_reader.hpp"
#include "opaque_attribute.hpp"

namespace cafe::detail {

/**
 * @brief Attribute parsing context with diagnostic location, declaration signature, and optional
 * Code-boundary validation hooks.
 * @ingroup implementation
 */
struct AttributeLocation {
  AttributeContext context; ///< Structural attribute location used for version/context validation.
  std::string_view path; ///< Borrowed structural path used in diagnostics.
  std::string_view descriptor = {}; ///< JVM field or method descriptor.
  bool code_required = false; ///< Whether the enclosing method must carry a Code attribute.
  bool is_static = false; ///< Whether the owning method has no receiver slot.
  bool annotation_element = false; ///< Whether the owning method is eligible for AnnotationDefault.
  const CodeAttributeContext* code = nullptr; ///< Optional borrowed bounded Code context for nested-attribute validation.
  CodeOffsetVisitor offsets = {}; ///< Callback visiting offset fields while validating nested Code attributes.
};

/**
 * @brief Bounded class parser owning speculative model storage and recoverable diagnostics.
 * Essential framing is validated before compatible optional-attribute recovery.
 * @ingroup implementation
 */
class ClassReader {
public:
  /**
   * @brief Initialize bounded class parser owning speculative model storage and recoverable
   * diagnostics
   * @param data Complete input class-file bytes; span input is copied and vector input is moved.
   * @param options Operation-specific parsing, emission, or entry policy.
   */
  ClassReader(std::vector<std::byte> data, const ReadOptions& options);
  /**
   * @brief Parse required structure and optional attributes into one owning class.
   * @return The class and recoverable diagnostics, or a fatal ReadError.
   */
  [[nodiscard]] std::expected<ReadResult, ReadError> read();

private:
  /**
   * @brief Inspect whether compatible recovery is disabled.
   * @return True for ParseMode::Strict.
   */
  [[nodiscard]] bool strict() const noexcept {
    return options.mode == ParseMode::Strict;
  }
  /**
   * @brief Read constant slots and original UTF-8 views under count and byte bounds.
   * @return Success or a tag/framing/encoding/resource error.
   */
  [[nodiscard]] std::expected<void, ReadError> read_constant_pool();
  /**
   * @brief Validate cross-references after all constant slots are available.
   * @return Success or an essential reference/version error.
   */
  [[nodiscard]] std::expected<void, ReadError> check_constant_pool() const;
  /**
   * @brief Validate dynamic constant bootstrap ordinals after attributes have been parsed.
   * @return Success or a missing/invalid bootstrap-table error.
   */
  [[nodiscard]] std::expected<void, ReadError> check_bootstraps() const;
  /**
   * @brief Read class identity, interfaces, declarations, and attributes after the constant pool.
   * @return Success or an essential structure/declaration/attribute error.
   */
  [[nodiscard]] std::expected<void, ReadError> read_envelope();

  /**
   * @brief Parse an ordered field or method table and bounded attributes.
   * @param members Field or method declarations in the required order.
   * @return Success or a framing/signature/duplicate/attribute error.
   */
  template<typename Member>
  [[nodiscard]] std::expected<void, ReadError> read_members(std::pmr::vector<Member>& members);

  /**
   * @brief Frame attributes before decoding and apply bounded compatible recovery.
   * @param parent Bounded enclosing cursor from which the attribute body is sliced.
   * @param attributes Destination table receiving successfully decoded or preserved attributes.
   * @param location Attribute/declaration context and structural diagnostic path.
   * @return Whether any attribute was dropped, or a fatal structural/strict-mode error.
   */
  [[nodiscard]] std::expected<bool, ReadError> read_attributes(
      BinaryReader& parent, std::pmr::vector<Attribute>& attributes, AttributeLocation location);
  /**
   * @brief Decode a typed payload or structurally validate a recognized opaque payload.
   * @param body Length-bounded attribute body reader.
   * @param name UTF-8 constant-pool reference for the name.
   * @param spec Registry rules for the recognized attribute.
   * @param location Attribute/declaration context and structural diagnostic path.
   * @param path Borrowed structural path included in diagnostics.
   * @return An owned attribute, or a bounded payload error.
   */
  [[nodiscard]] std::expected<Attribute, ReadError> decode_attribute(
      BinaryReader& body, CpIndex name, const AttributeSpec& spec, AttributeLocation location, std::string_view path);
  /**
   * @brief Decode a bounded historical/modern Code body with exception and metadata boundaries.
   * @param body Length-bounded attribute body reader.
   * @param name UTF-8 constant-pool reference for the name.
   * @param path Borrowed structural path included in diagnostics.
   * @param descriptor JVM field or method descriptor, or its UTF-8 constant-pool index.
   * @param is_static Whether the owning method has no implicit receiver slot.
   * @return Decoded Code, explicitly permitted opaque recovery, or a fatal read error.
   */
  [[nodiscard]] std::expected<Attribute, ReadError> read_code(
      BinaryReader& body, CpIndex name, std::string_view path, std::string_view descriptor, bool is_static);
  /**
   * @brief Check nested attribute framing before allowing an undecodable Code body to remain opaque.
   * @param parent Bounded enclosing cursor from which the attribute body is sliced.
   * @param path Borrowed structural path included in diagnostics.
   * @return Success or an essential envelope failure.
   */
  [[nodiscard]] std::expected<void, ReadError> check_opaque_code_envelopes(
      BinaryReader parent, std::string_view path);

  /**
   * @brief Append a recoverable diagnostic while enforcing the retained-message limit.
   * @param code Diagnostic classification describing the recovery or retained condition.
   * @param offset Byte offset into the supplied source/output range.
   * @param path Borrowed structural path included in diagnostics.
   * @param detail Explanation copied into the structured error.
   * @param action Requested add, replace, directory, or recovery action.
   */
  void diagnose(DiagnosticCode code, std::size_t offset, std::string_view path, std::string_view detail,
                RecoveryAction action);

  const ReadOptions& options; ///< Borrowed operation policy, which must outlive the internal codec.
  std::unique_ptr<ClassStorage> storage; ///< Owned backing storage for this object and its borrowed views.
  /**
   * @brief Bounded cursor borrowing the class storage's original input bytes.
   */
  BinaryReader input;
  std::pmr::vector<std::size_t> cp_offsets; ///< Original source offset for each parsed constant-pool slot.
  std::vector<Diagnostic> diagnostics; ///< Ordered recoverable conditions reported during the operation.
  std::size_t attribute_count = 0; ///< Cumulative attribute envelopes visited during parsing.
  bool has_dynamics = false; ///< Whether dynamic constants require a later bootstrap-table check.
  bool suppressed_diagnostics = false; ///< Whether the diagnostic limit has already been reported.
  bool has_opaque_code = false; ///< Whether essential Code was retained undecoded under explicit compatible policy.
};

/**
 * @brief Narrow a previously bounded class input range into its u4 source-slice representation.
 * @param offset Byte offset into the supplied source/output range.
 * @param size Requested byte count or checked input byte length.
 * @return A source slice; caller guarantees offset and size fit u4.
 * @ingroup implementation
 */
inline SourceSlice source_slice_of(std::size_t offset, std::size_t size) noexcept {
  // Every reader range is within a class whose input length fits u4.
  return {static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(size)};
}

} // namespace cafe::detail
