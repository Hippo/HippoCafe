#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cafe/error.hpp>
#include <cafe/ids.hpp>

namespace cafe {
class ConstantPool;
class MethodCode;

namespace analysis {
class FrameAnalysis;
}
} // namespace cafe

namespace cafe::detail {

// The frame encoder stages new constants beside the original pool. Existing
// references, including every ldc operand, retain their original indices.
class StackMapConstants {
public:
  StackMapConstants(const ConstantPool& pool, std::pmr::memory_resource* resource);

  [[nodiscard]] std::span<const std::byte> bytes() const {
    return extra;
  }
  [[nodiscard]] std::uint16_t count() const;
  [[nodiscard]] std::expected<CpIndex, WriteError> utf8(std::string_view text, std::string_view path);
  [[nodiscard]] std::expected<CpIndex, WriteError> class_reference(std::string_view name, std::string_view path);

private:
  void index_original_pool();

  const ConstantPool& pool;
  std::pmr::monotonic_buffer_resource names;
  std::pmr::unordered_map<std::string_view, CpIndex> utf8_indices;
  std::pmr::unordered_map<std::string_view, CpIndex> class_indices;
  std::pmr::vector<std::byte> extra;
  std::size_t next_index;
  bool indexed = false;
};

class InstructionOffsets {
public:
  InstructionOffsets(const MethodCode& code, std::span<const std::uint32_t> offsets,
                     std::pmr::memory_resource* scratch);

  [[nodiscard]] std::optional<std::uint16_t> offset_of(InstructionId id) const;

private:
  struct Entry {
    InstructionId instruction;
    std::uint16_t offset;
  };
  std::pmr::vector<Entry> entries;
};

[[nodiscard]] std::expected<std::pmr::vector<std::byte>, WriteError> encode_stack_map(
    const analysis::FrameAnalysis& analysis, const MethodCode& code, const InstructionOffsets& offsets,
    StackMapConstants& constants, std::pmr::memory_resource* scratch, std::string_view path);

} // namespace cafe::detail
