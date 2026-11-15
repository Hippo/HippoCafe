/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cafe/bytecode/instruction.hpp"
#include "cafe/class_version.hpp"
#include "cafe/constant_pool.hpp"

namespace cafe::detail {

/**
 * @brief Static operand-validation explanation and whether the failure originates in the constant
 * pool.
 * @ingroup implementation
 */
struct OperandError {
  std::string_view reason; ///< Static operand-validation explanation.
  bool constant_pool; ///< Whether the failure originates in a constant reference rather than instruction shape.
};

/**
 * @brief Validate opcode legality, local bounds, and constant operands for emission or analysis.
 * @param instruction Logical instruction value; scoped operands require the owning method.
 * @param version Exact JVM class-file major and minor version.
 * @param pool Owning class constant pool used for reference validation.
 * @param max_locals Maximum local-variable size in JVM slots.
 * @return An operand error, or nullopt for a valid instruction.
 * @ingroup implementation
 */
[[nodiscard]] std::optional<OperandError> validate_instruction(Instruction instruction, ClassVersion version,
                                                               const ConstantPool& pool, std::uint16_t max_locals);

/**
 * @brief Compute padding after a switch opcode to the next four-byte boundary.
 * @param offset Byte offset into the supplied source/output range.
 * @return A byte count from zero through three.
 * @ingroup implementation
 */
[[nodiscard]] constexpr std::uint32_t switch_padding(std::uint32_t offset) {
  return (3 - (offset & 3));
}

} // namespace cafe::detail
