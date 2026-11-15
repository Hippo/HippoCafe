#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cafe/bytecode/instruction.hpp"
#include "cafe/class_version.hpp"
#include "cafe/constant_pool.hpp"

namespace cafe::detail {

struct OperandError {
  std::string_view reason;
  bool constant_pool;
};

[[nodiscard]] std::optional<OperandError> validate_instruction(Instruction instruction, ClassVersion version,
                                                               const ConstantPool& pool, std::uint16_t max_locals);

[[nodiscard]] constexpr std::uint32_t switch_padding(std::uint32_t offset) {
  return (3 - (offset & 3));
}

} // namespace cafe::detail
