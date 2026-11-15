#pragma once

#include <expected>
#include <span>
#include <string_view>

#include <cafe/io/class_writer.hpp>

#include "binary_writer.hpp"
#include "opaque_attribute.hpp"

namespace cafe::detail {

struct WriteLocation {
  AttributeContext context;
  std::string_view path;
  std::string_view descriptor = {};
  bool code_required = false;
  bool is_static = false;
  bool annotation_element = false;
  bool rewritten_code = false;
  const CodeAttributeContext* code = nullptr;
};

class ClassWriter {
public:
  ClassWriter(const ClassFile& model, const WriteOptions& options);
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
  void write_code_header(const CodeAttribute& code, std::size_t length);

  const ClassFile& model;
  const WriteOptions& options;
  const ConstantPool& pool;
  ClassVersion version;
  WriteResult result;
  BinaryWriter<std::vector<std::byte>> output;
};

[[nodiscard]] inline std::unexpected<WriteError> write_error(
    WriteErrorKind kind, std::string_view path, std::string_view detail) {
  return std::unexpected(WriteError{kind, std::string(path), std::string(detail)});
}

} // namespace cafe::detail
