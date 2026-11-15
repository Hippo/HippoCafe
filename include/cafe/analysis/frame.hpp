#pragma once

#include <memory_resource>
#include <vector>

#include "cafe/analysis/verification_type.hpp"

namespace cafe::analysis {

struct Frame {
  explicit Frame(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) :
      locals(resource), stack(resource) {
  }

  // A two-slot local ends with WideContinuation. The stack instead stores
  // one entry per value, in bottom-to-top order.
  std::pmr::vector<VerificationType> locals;
  std::pmr::vector<VerificationType> stack;
  unsigned stack_slots = 0;
  bool constructor_uninitialized = false;
};

struct StackMapFrame {
  InstructionId instruction;
  Frame frame;
};

} // namespace cafe::analysis
