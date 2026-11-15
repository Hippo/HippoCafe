#pragma once

#include <span>

#include "code_layout.hpp"

namespace cafe::detail {

class EncodedCode {
public:
  [[nodiscard]] std::span<const std::byte> bytes() const {
    return output;
  }
  [[nodiscard]] std::span<const std::uint8_t> instruction_starts() const {
    return starts;
  }
  [[nodiscard]] std::span<const std::uint32_t> instruction_offsets() const {
    return layout.offsets;
  }
  [[nodiscard]] std::optional<std::uint32_t> offset_of(LabelId id) const {
    return layout.offset_of(id);
  }
  [[nodiscard]] bool has_expanded_conditionals() const {
    return !layout.expanded_conditionals.empty();
  }

private:
  explicit EncodedCode(CodeLayout layout, std::pmr::memory_resource* scratch);

  friend std::expected<EncodedCode, WriteError> encode_code(
      const MethodCode&, ClassVersion, const ConstantPool&, std::uint16_t, bool,
      std::pmr::memory_resource*, std::string_view, bool);

  CodeLayout layout;
  std::pmr::vector<std::byte> output;
  std::pmr::vector<std::uint8_t> starts;
};

// The returned arrays use scratch. Keep the resource alive through any
// exception-table and metadata emission that uses their resolved offsets.
[[nodiscard]] std::expected<EncodedCode, WriteError> encode_code(
    const MethodCode& code, ClassVersion version, const ConstantPool& pool,
    std::uint16_t max_locals, bool preserve_original_encodings,
    std::pmr::memory_resource* scratch, std::string_view path, bool expand_conditionals = false);

} // namespace cafe::detail
