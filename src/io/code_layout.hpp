#pragma once

#include <cstdint>
#include <expected>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>

#include "cafe/bytecode/opcode.hpp"
#include "cafe/class_version.hpp"
#include "cafe/error.hpp"
#include "cafe/ids.hpp"

namespace cafe {

class ConstantPool;
class MethodCode;

namespace detail {

struct CodeLayout {
  explicit CodeLayout(std::pmr::memory_resource* scratch) :
      encodings(scratch), offsets(scratch), label_offsets(scratch), expanded_conditionals(scratch) {
  }

  std::pmr::vector<Opcode> encodings;
  // The last offset is the exclusive end.
  std::pmr::vector<std::uint32_t> offsets;
  std::pmr::vector<std::uint32_t> label_offsets;
  std::pmr::vector<std::uint8_t> expanded_conditionals;
  std::uint32_t scope = 0;

  [[nodiscard]] std::optional<std::uint32_t> offset_of(LabelId id) const;
  [[nodiscard]] bool expanded(std::size_t index) const {
    return !expanded_conditionals.empty() && expanded_conditionals[index] != 0;
  }
};

[[nodiscard]] std::expected<CodeLayout, WriteError> layout_code(
    const MethodCode& code, ClassVersion version, const ConstantPool& pool,
    std::uint16_t max_locals, bool preserve_original_encodings,
    std::pmr::memory_resource* scratch, std::string_view path, bool expand_conditionals = false);

} // namespace detail
} // namespace cafe
