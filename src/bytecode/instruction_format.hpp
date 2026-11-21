/**
 * @file
 * @brief Shared instruction formatting for diagnostics and SSA dumps.
 * @ingroup implementation
 */

#pragma once

#include <iosfwd>

#include <cafe/bytecode/instruction.hpp>

namespace cafe::detail {

/**
 * @brief Write a JVM mnemonic, with a numeric fallback for reserved opcode bytes.
 * @param out Destination stream.
 * @param opcode JVM opcode to render.
 */
void write_opcode(std::ostream& out, Opcode opcode);

/**
 * @brief Write the typed immediate, local, constant, or array operands of a raw instruction.
 * @param out Destination stream.
 * @param instruction Raw instruction supplying the operands.
 */
void write_raw_operands(std::ostream& out, const Instruction& instruction);

} // namespace cafe::detail
