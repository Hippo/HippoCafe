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

struct WriteLocation {
  AttributeContext context;
  std::string_view path;
  std::string_view descriptor = {};
  bool code_required = false;
  bool is_static = false;
  bool annotation_element = false;
  bool rewritten_code = false;
  const CodeAttributeContext* code = nullptr;
  const Method* method = nullptr;
};

struct WireExceptionHandler {
  std::uint16_t start;
  std::uint16_t end;
  std::uint16_t handler;
  CpIndex catch_type;
};

class ClassWriter {
public:
  ClassWriter(const ClassFile& model, const WriteOptions& options, analysis::AnalysisContext* analysis_context = nullptr);
  [[nodiscard]] std::expected<WriteResult, WriteError> write();

private:
  [[nodiscard]] std::expected<void, WriteError> write_constant_pool();
  template<typename Member>
  [[nodiscard]] std::expected<void, WriteError> write_members(std::span<const Member> members);
  [[nodiscard]] std::expected<void, WriteError> write_attributes(
      std::span<const Attribute> attributes, WriteLocation location);
  [[nodiscard]] std::expected<void, WriteError> write_attribute_body(
      const Attribute& attribute, const AttributeSpec* spec, WriteLocation location, std::string_view path);
  [[nodiscard]] std::expected<void, WriteError> write_code(
      const CodeAttribute& code, WriteLocation location, std::string_view path);
  [[nodiscard]] std::expected<void, WriteError> write_analyzed_code(
      const CodeAttribute& code, const Method& method, std::string_view path);
  [[nodiscard]] std::expected<void, WriteError> write_analyzed_code_attributes(
      const CodeAttribute& code, const EncodedCode& encoded, const CodeAttributeContext& context,
      CpIndex stack_map_name, std::span<const std::byte> stack_map,
      bool rewritten, std::pmr::memory_resource* scratch, std::string_view path);
  [[nodiscard]] std::expected<WireExceptionHandler, WriteError> resolve_handler(
      const ExceptionHandler& handler, const EncodedCode& encoded, std::string_view path) const;
  [[nodiscard]] std::expected<CodeAttributeContext, WriteError> write_code_array(
      const CodeAttribute& code, const EncodedCode& encoded, std::uint16_t max_stack, std::uint16_t max_locals,
      std::string_view path);
  void write_code_header(std::uint16_t max_stack, std::uint16_t max_locals, std::size_t length);

  const ClassFile& model;
  const WriteOptions& options;
  analysis::AnalysisContext* analysis_context;
  std::optional<analysis::AnalysisContext> default_analysis_context;
  const ConstantPool& pool;
  ClassVersion version;
  BudgetResource analysis_budget;
  StackMapConstants stack_map_constants;
  WriteResult result;
  BinaryWriter<std::vector<std::byte>> output;
};

[[nodiscard]] inline std::unexpected<WriteError> write_error(
    WriteErrorKind kind, std::string_view path, std::string_view detail) {
  return std::unexpected(WriteError{kind, std::string(path), std::string(detail)});
}

} // namespace cafe::detail
