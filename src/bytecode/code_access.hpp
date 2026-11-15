#pragma once

#include <span>

#include "cafe/bytecode/code.hpp"

namespace cafe::detail {

struct BytecodeAccess {
  static Instruction with_hint(Instruction value, EncodingHint hint) {
    value.hint = hint;
    return value;
  }

  static std::uint32_t scope(const MethodCode& code);
  static std::size_t slot_count(const MethodCode& code);
  static std::size_t label_count(const MethodCode& code);

  // Reader-only helpers to attach original metadata boundaries without
  // recording a user edit.
  static std::expected<LabelId, CodeEditError> original_label(MethodCode& code, CodePosition position);
  static void mark_clean(MethodCode& code);

  static void set_original_switch_padding(MethodCode& code, InstructionId id, std::span<const std::byte> padding);
  static std::span<const std::byte> original_switch_padding(const MethodCode& code, InstructionId id);
};

} // namespace cafe::detail
