/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <expected>
#include <span>
#include <string_view>

#include <cafe/analysis/context.hpp>
#include <cafe/io/class_writer.hpp>

#include "binary_writer.hpp"
#include "model/budget_resource.hpp"
#include "opaque_attribute.hpp"
#include "stack_map_encoder.hpp"

namespace cafe::detail {

class EncodedCode;

/**
 * @brief Attribute emission context with its owning declaration, diagnostic path, and optional
 * final code-offset constraints.
 * @ingroup implementation
 */
struct WriteLocation {
  AttributeContext context; ///< Structural attribute location used for version/context validation.
  std::string_view path; ///< Borrowed structural path used in diagnostics.
  std::string_view descriptor = {}; ///< JVM field or method descriptor.
  bool code_required = false; ///< Whether the enclosing method must carry a Code attribute.
  bool is_static = false; ///< Whether the owning method has no receiver slot.
  bool annotation_element = false; ///< Whether the owning method is eligible for AnnotationDefault.
  bool rewritten_code = false; ///< Whether code/offset dependencies changed from their original representation.
  const CodeAttributeContext* code = nullptr; ///< Optional borrowed bounded Code context for nested-attribute validation.
  const Method* method = nullptr; ///< Borrowed owning method declaration.
};

/**
 * @brief Resolved u2 code offsets and catch-type reference for one emitted exception-table record.
 * @ingroup implementation
 */
struct WireExceptionHandler {
  std::uint16_t start; ///< Inclusive protected-range byte offset in the emitted code array.
  std::uint16_t end; ///< Exclusive protected-range byte offset in the emitted code array.
  std::uint16_t handler; ///< Handler entry byte offset in the emitted code array.
  CpIndex catch_type; ///< Exception Class reference; zero denotes a catch-all handler.
};

/**
 * @brief Nonmutating staged class serializer. It validates the canonical model, owns output and
 * frame-constant staging, and emits no partial public result on failure.
 * @ingroup implementation
 */
class ClassWriter {
public:
  /**
   * @brief Initialize nonmutating staged class serializer
   * @param model Canonical class model, borrowed without mutation.
   * @param options Operation-specific parsing, emission, or entry policy.
   * @param analysis_context Optional hierarchy and limits for analysis. A null pointer
   *   uses default limits and starts frame computation with an empty classpath.
   */
  ClassWriter(const ClassFile& model, const WriteOptions& options, analysis::AnalysisContext* analysis_context = nullptr);
  /**
   * @brief Validate the class and serialize it into owned output storage.
   * @return Complete WriteResult or structured WriteError; the input model is unchanged.
   */
  [[nodiscard]] std::expected<WriteResult, WriteError> write();

private:
  /**
   * @brief Validate and serialize original constants and staged frame extensions.
   * @return Success or a constant-tag/reference/version/count error.
   */
  [[nodiscard]] std::expected<void, WriteError> write_constant_pool();
  /**
   * @brief Validate and serialize one ordered member table and its attributes.
   * @param members Field or method declarations in the required order.
   * @return Success or a declaration/attribute error.
   */
  template<typename Member>
  [[nodiscard]] std::expected<void, WriteError> write_members(std::span<const Member> members);
  /**
   * @brief Validate locations, versions, names, and duplicate rules before serializing attributes.
   * @param attributes Ordered attribute views borrowed for validation and serialization.
   * @param location Attribute/declaration context and structural diagnostic path.
   * @return Success or an unsafe/invalid attribute error.
   */
  [[nodiscard]] std::expected<void, WriteError> write_attributes(
      std::span<const Attribute> attributes, WriteLocation location);
  /**
   * @brief Emit a typed payload or validated raw attribute body.
   * @param attribute Attribute payload being validated or emitted.
   * @param spec Registry rules for the recognized attribute.
   * @param location Attribute/declaration context and structural diagnostic path.
   * @param path Borrowed structural path included in diagnostics.
   * @return Success or an invalid/unsafe/oversized payload error.
   */
  [[nodiscard]] std::expected<void, WriteError> write_attribute_body(
      const Attribute& attribute, const AttributeSpec* spec, WriteLocation location, std::string_view path);
  /**
   * @brief Select unchanged Code preservation or validated caller-metadata re-encoding.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param location Attribute/declaration context and structural diagnostic path.
   * @param path Borrowed structural path included in diagnostics.
   * @return Success or stale/unsafe/invalid Code metadata error.
   */
  [[nodiscard]] std::expected<void, WriteError> write_code(
      const CodeAttribute& code, WriteLocation location, std::string_view path);
  /**
   * @brief Build an emission plan and compute maxima or frames without mutating canonical Code.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param method Method belonging to the supplied class.
   * @param path Borrowed structural path included in diagnostics.
   * @return Success or a structured analysis/layout/metadata error.
   */
  [[nodiscard]] std::expected<void, WriteError> write_analyzed_code(
      const CodeAttribute& code, const Method& method, std::string_view path);
  /**
   * @brief Emit fresh frames and relocate supported offset-bearing nested attributes.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param encoded Final encoded code and its resolved offset maps.
   * @param context Optional bounded Code bytes, instruction boundaries, local limit, and
   *   handler-count context.
   * @param stack_map_name One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @param stack_map Fresh encoded StackMapTable body.
   * @param rewritten Whether code/metadata dependencies changed from the original payload.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   * @param path Borrowed structural path included in diagnostics.
   * @return Success or an unsafe/unmapped attribute error; explicit dropping emits diagnostics.
   */
  [[nodiscard]] std::expected<void, WriteError> write_analyzed_code_attributes(
      const CodeAttribute& code, const EncodedCode& encoded, const CodeAttributeContext& context,
      CpIndex stack_map_name, std::span<const std::byte> stack_map,
      bool rewritten, std::pmr::memory_resource* scratch, std::string_view path);
  /**
   * @brief Resolve scoped handler labels into final u2 code offsets.
   * @param handler Exception-table entry whose labels or type are resolved.
   * @param encoded Final encoded code and its resolved offset maps.
   * @param path Borrowed structural path included in diagnostics.
   * @return A wire handler, or an invalid range/type/label error.
   */
  [[nodiscard]] std::expected<WireExceptionHandler, WriteError> resolve_handler(
      const ExceptionHandler& handler, const EncodedCode& encoded, std::string_view path) const;
  /**
   * @brief Write limits, encoded instructions, and resolved handlers.
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param encoded Final encoded code and its resolved offset maps.
   * @param max_stack Maximum operand-stack size in JVM slots.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @param path Borrowed structural path included in diagnostics.
   * @return A context for nested attribute validation, or a Code layout error.
   */
  [[nodiscard]] std::expected<CodeAttributeContext, WriteError> write_code_array(
      const CodeAttribute& code, const EncodedCode& encoded, std::uint16_t max_stack, std::uint16_t max_locals,
      std::string_view path);
  /**
   * @brief Emit historical or modern Code maxima and code-length fields.
   * @param max_stack Maximum operand-stack size in JVM slots.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @param length Number of bytes in the bounded range or encoded payload.
   */
  void write_code_header(std::uint16_t max_stack, std::uint16_t max_locals, std::size_t length);

  const ClassFile& model; ///< Borrowed canonical class model; writer operations do not mutate it.
  const WriteOptions& options; ///< Borrowed operation policy, which must outlive the internal codec.
  analysis::AnalysisContext* analysis_context; ///< Optional borrowed context for writer verification metadata computation.
  std::optional<analysis::AnalysisContext> default_analysis_context; ///< Context with an empty classpath for frame computation without a supplied context.
  const ConstantPool& pool; ///< Borrowed owning class constant pool used for lookup and validation.
  ClassVersion version; ///< Exact class-file major and minor version.
  BudgetResource analysis_budget; ///< Shared writer analysis allocation budget.
  StackMapConstants stack_map_constants; ///< Writer-local constant-pool extension for frame reference names.
  WriteResult result; ///< Owned serialized bytes and diagnostics accumulated for the final result.
  BinaryWriter<std::vector<std::byte>> output; ///< Byte writer borrowing result.bytes.
};

/**
 * @brief Construct a writer failure with owned diagnostic strings.
 * @param kind Payload or error discriminator.
 * @param path Borrowed structural path included in diagnostics.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected WriteError.
 * @ingroup implementation
 */
[[nodiscard]] inline std::unexpected<WriteError> write_error(
    WriteErrorKind kind, std::string_view path, std::string_view detail) {
  return std::unexpected(WriteError{kind, std::string(path), std::string(detail)});
}

} // namespace cafe::detail
