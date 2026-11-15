/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <variant>

#include "cafe/bytecode/instruction.hpp"
#include "cafe/bytecode/switch.hpp"

namespace cafe {

/**
 * @brief Insertion descriptor retaining scoped branch/switch labels until ownership validation.
 *
 * Descriptors keep scoped labels until MethodCode verifies ownership. The
 * stored Instruction itself only needs the compact method-local operand.
 */
using InstructionBuilder = std::variant<Instruction, Branch, TableSwitch, LookupSwitch>;

} // namespace cafe
