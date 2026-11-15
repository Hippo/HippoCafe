/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <memory_resource>
#include <vector>

#include "cafe/analysis/verification_type.hpp"

namespace cafe::analysis {

/**
 * @brief Mutable verification state. Locals include WideContinuation slots; the stack stores one
 * entry per value in bottom-to-top order. Its resource must outlive its vectors, and reference
 * names retain their separate owners.
 * @ingroup analysis
 */
struct Frame {
  /**
   * @brief Create empty local and stack vectors using the supplied resource.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  explicit Frame(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) :
      locals(resource), stack(resource) {
  }

  /**
   * @brief Local slots, including WideContinuation after long/double values.
   *
   * A two-slot local ends with WideContinuation. The stack instead stores
   * one entry per value, in bottom-to-top order.
   */
  std::pmr::vector<VerificationType> locals;
  std::pmr::vector<VerificationType> stack; ///< Operand values in bottom-to-top order, one entry per value.
  unsigned stack_slots = 0; ///< Total JVM slots occupied by the operand stack.
  bool constructor_uninitialized = false; ///< Whether an instance constructor still requires receiver initialization.
};

/**
 * @brief Verification state at the boundary before a particular instruction.
 * @ingroup analysis
 */
struct StackMapFrame {
  InstructionId instruction; ///< Instruction identity associated with this record.
  Frame frame; ///< Verification state before the associated instruction.
};

} // namespace cafe::analysis
