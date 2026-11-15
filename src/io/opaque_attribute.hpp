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

struct CodeAttributeContext {
  std::span<const std::byte> bytes;
  // Indexed by byte offset into bytes. Nonzero entries mark opcode boundaries.
  std::span<const std::uint8_t> instruction_starts;
  std::uint16_t max_locals;
  std::uint16_t exception_count;
};

// The caller gates version and context before validating the bounded body.
// Validated attributes still retain their complete original bytes.
[[nodiscard]] std::expected<void, ReadError> validate_opaque_attribute(
    BinaryReader& body, const AttributeSpec& spec, AttributeContext context,
    const ConstantPool& cp, const ReadLimits& limits, std::string_view path,
    const CodeAttributeContext* code = nullptr,
    std::pmr::memory_resource* scratch = std::pmr::get_default_resource());

} // namespace detail
} // namespace cafe
