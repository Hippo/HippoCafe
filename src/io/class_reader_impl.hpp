#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <cafe/io/class_reader.hpp>

#include "../model/class_storage.hpp"
#include "binary_reader.hpp"
#include "opaque_attribute.hpp"

namespace cafe::detail {

struct AttributeLocation {
  AttributeContext context;
  std::string_view path;
  std::string_view descriptor = {};
  bool code_required = false;
  bool is_static = false;
  bool annotation_element = false;
  const CodeAttributeContext* code = nullptr;
};

class ClassReader {
public:
  ClassReader(std::vector<std::byte> data, const ReadOptions& options);
  [[nodiscard]] std::expected<ReadResult, ReadError> read();

private:
  [[nodiscard]] bool strict() const noexcept {
    return options.mode == ParseMode::Strict;
  }
  [[nodiscard]] std::expected<void, ReadError> read_constant_pool();
  [[nodiscard]] std::expected<void, ReadError> check_constant_pool() const;
  [[nodiscard]] std::expected<void, ReadError> check_bootstraps() const;
  [[nodiscard]] std::expected<void, ReadError> read_envelope();

  template<typename Member>
  [[nodiscard]] std::expected<void, ReadError> read_members(std::pmr::vector<Member>& members);

  [[nodiscard]] std::expected<bool, ReadError> read_attributes(
      BinaryReader& parent, std::pmr::vector<Attribute>& attributes, AttributeLocation location);
  [[nodiscard]] std::expected<Attribute, ReadError> decode_attribute(
      BinaryReader& body, CpIndex name, const AttributeSpec& spec, AttributeLocation location, std::string_view path);
  [[nodiscard]] std::expected<Attribute, ReadError> read_code(
      BinaryReader& body, CpIndex name, std::string_view path, std::string_view descriptor, bool is_static);
  [[nodiscard]] std::expected<void, ReadError> check_opaque_code_envelopes(
      BinaryReader parent, std::string_view path);

  void diagnose(DiagnosticCode code, std::size_t offset, std::string_view path, std::string_view detail,
                RecoveryAction action);

  const ReadOptions& options;
  std::unique_ptr<ClassStorage> storage;
  BinaryReader input;
  std::pmr::vector<std::size_t> cp_offsets;
  std::vector<Diagnostic> diagnostics;
  std::size_t attribute_count = 0;
  bool has_dynamics = false;
  bool suppressed_diagnostics = false;
  bool has_opaque_code = false;
};

inline SourceSlice source_slice_of(std::size_t offset, std::size_t size) noexcept {
  // Every reader range is within a class whose input length fits u4.
  return {static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(size)};
}

} // namespace cafe::detail
