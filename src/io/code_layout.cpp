#include "code_layout.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "cafe/bytecode/code.hpp"

#include "bytecode/code_access.hpp"
#include "code_operands.hpp"

namespace cafe::detail {
namespace {

constexpr std::uint32_t NO_POSITION = std::numeric_limits<std::uint32_t>::max();
constexpr unsigned MAX_LAYOUT_PASSES = 64;

bool can_widen(Opcode logical) {
  return logical == Opcode::Goto || logical == Opcode::Jsr;
}

Opcode wide_branch(Opcode logical) {
  return logical == Opcode::Goto ? Opcode::GotoW : Opcode::JsrW;
}

Opcode select_encoding(Instruction instruction, bool preserve) {
  auto opcode = instruction.opcode();
  auto hint = preserve ? instruction.encoding_hint() : EncodingHint::Automatic;
  switch (opcode_info(opcode).form) {
    case OperandForm::Local: {
      auto local = *instruction.local_index();
      if (local > 255 || hint == EncodingHint::Wide)
        return Opcode::Wide;
      if (local <= 3 && opcode != Opcode::Ret && hint != EncodingHint::Explicit) {
        auto base = opcode <= Opcode::ALoad ? Opcode::ILoad : Opcode::IStore;
        auto implicit_base = base == Opcode::ILoad ? Opcode::ILoad0 : Opcode::IStore0;
        auto family = static_cast<unsigned>(opcode) - static_cast<unsigned>(base);
        return static_cast<Opcode>(static_cast<unsigned>(implicit_base) + 4 * family + local);
      }
      break;
    }
    case OperandForm::Increment: {
      auto increment = *instruction.increment_value();
      if (increment.local > 255 || !std::in_range<std::int8_t>(increment.amount) || hint == EncodingHint::Wide)
        return Opcode::Wide;
      break;
    }
    case OperandForm::ConstantByte:
      if (instruction.constant_index()->value > 255 || hint == EncodingHint::Wide)
        return Opcode::LdcW;
      break;
    case OperandForm::BranchShort:
      if (hint == EncodingHint::Wide && can_widen(opcode))
        return wide_branch(opcode);
      break;
    default:
      break;
  }
  return opcode;
}

struct BranchOffsets {
  std::uint32_t source;
  std::uint32_t target;
};

} // namespace

std::optional<std::uint32_t> CodeLayout::offset_of(LabelId id) const {
  if (id.scope != scope || id.value >= label_offsets.size() || label_offsets[id.value] == NO_POSITION)
    return std::nullopt;
  return label_offsets[id.value];
}

std::expected<CodeLayout, WriteError> layout_code(const MethodCode& code, ClassVersion version,
                                                  const ConstantPool& pool, std::uint16_t max_locals,
                                                  bool preserve_original_encodings,
                                                  std::pmr::memory_resource* scratch, std::string_view path,
                                                  bool expand_conditionals) {
  auto fail = [&](WriteErrorKind kind, std::string_view reason) {
    return std::unexpected(WriteError{kind, std::string(path), std::string(reason)});
  };
  if (code.size() == 0)
    return fail(WriteErrorKind::InvalidCodeLayout, "code must contain at least one instruction");
  if (code.size() > MAX_CODE_SIZE)
    return fail(WriteErrorKind::CodeTooLarge, "instruction count exceeds the 65535-byte code limit");

  const auto count = static_cast<std::uint32_t>(code.size());
  CodeLayout layout(scratch);
  layout.scope = BytecodeAccess::scope(code);
  layout.label_offsets.assign(BytecodeAccess::label_count(code), NO_POSITION);
  layout.encodings.resize(count);
  layout.offsets.resize(count + 1);
  std::pmr::vector<std::uint32_t> widths(count, scratch);
  std::pmr::vector<std::uint32_t> positions_by_slot(scratch);
  if (!layout.label_offsets.empty()) {
    positions_by_slot.resize(BytecodeAccess::slot_count(code), NO_POSITION);
    for (std::uint32_t i = 0; i < count; ++i)
      positions_by_slot[code.at(i).index] = i;
  }
  // Initially label_offsets contains logical positions; the fixed-point pass
  // converts them to byte offsets after widths and padding settle.
  for (std::uint32_t i = 0; i < layout.label_offsets.size(); ++i) {
    auto position = code.label_position(LabelId{i, layout.scope});
    if (!position)
      continue;
    layout.label_offsets[i] = position->before ? positions_by_slot[position->before->index] : count;
  }

  const auto executable = [&](LabelId id) {
    auto position = layout.offset_of(id);
    return position && *position < count;
  };
  std::pmr::vector<BranchOffsets> branches(scratch);
  for (std::uint32_t i = 0; i < count; ++i) {
    auto id = code.at(i);
    auto instruction = *code.get(id);
    if (auto error = validate_instruction(instruction, version, pool, max_locals))
      return fail(error->constant_pool ? WriteErrorKind::InvalidConstantPoolReference : WriteErrorKind::InvalidInstruction,
                  error->reason);

    auto wire = select_encoding(instruction, preserve_original_encodings);
    layout.encodings[i] = wire;
    auto form = opcode_info(wire).form;
    switch (form) {
      case OperandForm::WidePrefix:
        widths[i] = instruction.opcode() == Opcode::IInc ? 6 : 4;
        break;
      case OperandForm::BranchShort:
      case OperandForm::BranchInt: {
        auto target = code.branch_target(id);
        auto position = target ? layout.offset_of(*target) : std::nullopt;
        if (!position || *position >= count)
          return fail(WriteErrorKind::InvalidLabel, "branch requires a bound instruction target");
        branches.push_back({i, *position});
        widths[i] = opcode_info(wire).wire_width;
        break;
      }
      case OperandForm::TableSwitch: {
        auto switch_id = code.switch_id(id);
        auto table = switch_id ? code.table_switch(*switch_id) : std::unexpected(CodeEditError::InvalidSwitch);
        if (!table || table->targets.empty() || !executable(table->default_target) ||
            !std::ranges::all_of(table->targets, executable))
          return fail(WriteErrorKind::InvalidSwitchTargets, "tableswitch requires bound instruction targets");
        if (table->targets.size() > (MAX_CODE_SIZE - 13) / 4)
          return fail(WriteErrorKind::CodeTooLarge, "tableswitch does not fit in a JVM code array");
        widths[i] = 13 + 4 * static_cast<std::uint32_t>(table->targets.size());
        break;
      }
      case OperandForm::LookupSwitch: {
        auto switch_id = code.switch_id(id);
        auto lookup = switch_id ? code.lookup_switch(*switch_id) : std::unexpected(CodeEditError::InvalidSwitch);
        if (!lookup || !executable(lookup->default_target) ||
            !std::ranges::all_of(lookup->pairs, [&](const LookupPair& pair) { return executable(pair.target); }))
          return fail(WriteErrorKind::InvalidSwitchTargets, "lookupswitch requires bound instruction targets");
        if (lookup->pairs.size() > (MAX_CODE_SIZE - 9) / 8)
          return fail(WriteErrorKind::CodeTooLarge, "lookupswitch does not fit in a JVM code array");
        widths[i] = 9 + 8 * static_cast<std::uint32_t>(lookup->pairs.size());
        break;
      }
      default:
        widths[i] = opcode_info(wire).wire_width;
        break;
    }
  }

  for (unsigned pass = 0; pass < MAX_LAYOUT_PASSES; ++pass) {
    std::uint32_t offset = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      layout.offsets[i] = offset;
      auto width = widths[i];
      auto wire = layout.encodings[i];
      if (wire == Opcode::TableSwitch || wire == Opcode::LookupSwitch)
        width += switch_padding(offset);
      if (width > MAX_CODE_SIZE - offset)
        return fail(WriteErrorKind::CodeTooLarge, "encoded code exceeds 65535 bytes");
      offset += width;
    }
    layout.offsets.back() = offset;

    bool changed = false;
    bool conditional_overflow = false;
    for (auto branch : branches) {
      auto& wire = layout.encodings[branch.source];
      auto displacement = static_cast<std::int64_t>(layout.offsets[branch.target]) - layout.offsets[branch.source];
      if (layout.expanded(branch.source) || opcode_info(wire).form == OperandForm::BranchInt ||
          std::in_range<std::int16_t>(displacement))
        continue;
      if (can_widen(wire)) {
        wire = wide_branch(wire);
        widths[branch.source] = opcode_info(wire).wire_width;
        changed = true;
      } else if (expand_conditionals && branch.source + 1 < count) {
        if (layout.expanded_conditionals.empty())
          layout.expanded_conditionals.resize(count);
        layout.expanded_conditionals[branch.source] = 1;
        // Inverted if<cond> skips the five-byte goto_w to the fallthrough.
        widths[branch.source] = 8;
        changed = true;
      } else {
        conditional_overflow = true;
      }
    }

    if (changed)
      continue;
    if (conditional_overflow)
      return fail(WriteErrorKind::BranchOutOfRange, "conditional branch displacement exceeds the signed 16-bit range");
    for (auto& position : layout.label_offsets) {
      if (position != NO_POSITION)
        position = layout.offsets[position];
    }
    return layout;
  }
  return fail(WriteErrorKind::UnstableLayout, "code layout exceeded the branch-widening pass limit");
}

} // namespace cafe::detail
