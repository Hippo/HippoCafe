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
      encodings(scratch), offsets(scratch), label_offsets(scratch) {
  }

  std::pmr::vector<Opcode> encodings;
  // The last offset is the exclusive end.
  std::pmr::vector<std::uint32_t> offsets;
  std::pmr::vector<std::uint32_t> label_offsets;
  std::uint32_t scope = 0;

  [[nodiscard]] std::optional<std::uint32_t> offset_of(LabelId id) const;
};

[[nodiscard]] std::expected<CodeLayout, WriteError> layout_code(
    const MethodCode& code, ClassVersion version, const ConstantPool& pool,
    std::uint16_t max_locals, bool preserve_original_encodings,
    std::pmr::memory_resource* scratch, std::string_view path);

} // namespace detail
} // namespace cafe
