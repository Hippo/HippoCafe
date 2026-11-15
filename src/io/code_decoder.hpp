#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

#include "cafe/bytecode/code.hpp"
#include "cafe/class_version.hpp"
#include "cafe/error.hpp"
#include "cafe/read_options.hpp"

namespace cafe {

class ConstantPool;

namespace detail {

class CodeDecoder;

class DecodedBoundaries {
public:
  [[nodiscard]] bool is_instruction_start(std::size_t offset) const;
  [[nodiscard]] bool is_boundary(std::size_t offset) const;
  [[nodiscard]] std::span<const std::uint8_t> instruction_starts() const {
    return starts;
  }
  [[nodiscard]] std::expected<LabelId, CodeEditError> label_at(MethodCode& code, std::size_t offset);
  void retain_metadata_boundary(std::size_t offset);
  [[nodiscard]] std::size_t metadata_boundary_count() const {
    return metadata_boundaries;
  }
  [[nodiscard]] std::optional<std::size_t> metadata_ordinal(std::size_t offset) const;

private:
  friend class CodeDecoder;
  explicit DecodedBoundaries(std::size_t length, std::uint32_t scope, std::pmr::memory_resource* scratch);

  std::pmr::vector<std::uint8_t> starts;
  std::pmr::vector<std::uint16_t> ordinals;
  std::pmr::vector<LabelId> labels;
  std::uint32_t scope;
  std::size_t metadata_boundaries = 0;
};

// Decode into an empty temporary MethodCode. The returned boundary table
// borrows its allocator from scratch; code storage remains owned by the method.
[[nodiscard]] std::expected<DecodedBoundaries, ReadError> decode_code(
    MethodCode& code, std::span<const std::byte> bytes, std::size_t absolute_offset,
    ClassVersion version, const ConstantPool& pool, std::uint16_t max_locals,
    const ReadOptions& options, std::pmr::memory_resource* scratch, std::string_view path);

} // namespace detail
} // namespace cafe
