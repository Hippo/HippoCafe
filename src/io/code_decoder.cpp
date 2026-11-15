#include "code_decoder.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "binary_reader.hpp"
#include "bytecode/code_access.hpp"
#include "code_operands.hpp"

namespace cafe::detail {

DecodedBoundaries::DecodedBoundaries(std::size_t length, std::uint32_t scope, std::pmr::memory_resource* scratch) :
    starts(length, scratch), ordinals(length, scratch), labels(scratch), scope(scope) {
}

bool DecodedBoundaries::is_instruction_start(std::size_t offset) const {
  return offset < starts.size() && starts[offset] != 0;
}

bool DecodedBoundaries::is_boundary(std::size_t offset) const {
  return offset == starts.size() || is_instruction_start(offset);
}

void DecodedBoundaries::retain_metadata_boundary(std::size_t offset) {
  if (offset < starts.size() && starts[offset] == 1) {
    starts[offset] = 2;
    ++metadata_boundaries;
  }
}

std::optional<std::size_t> DecodedBoundaries::metadata_ordinal(std::size_t offset) const {
  if (offset < starts.size() && starts[offset] == 2)
    return ordinals[offset];
  return std::nullopt;
}

std::expected<LabelId, CodeEditError> DecodedBoundaries::label_at(MethodCode& code, std::size_t offset) {
  if (BytecodeAccess::scope(code) != scope || code.edited() || !is_boundary(offset))
    return std::unexpected(CodeEditError::InvalidLabel);
  if (labels.empty())
    labels.resize(starts.size() + 1);
  auto& id = labels[offset];
  if (id.scope == 0) {
    auto position = CodePosition{offset == starts.size() ? std::nullopt : std::optional(code.at(ordinals[offset]))};
    auto label = BytecodeAccess::original_label(code, position);
    if (!label)
      return std::unexpected(label.error());
    id = *label;
  }
  return id;
}

class CodeDecoder {
public:
  CodeDecoder(MethodCode& code, std::span<const std::byte> bytes, std::size_t base_offset,
              ClassVersion version, const ConstantPool& pool, std::uint16_t max_locals,
              const ReadOptions& options, std::pmr::memory_resource* scratch, std::string_view path) :
      code(code), reader(bytes, base_offset), version(version), pool(pool), max_locals(max_locals),
      options(options), scratch(scratch), path(path), offsets(bytes.size(), BytecodeAccess::scope(code), scratch), pending(scratch) {
  }

  std::expected<DecodedBoundaries, ReadError> run() {
    // Code size bounds the number of decoded instructions and target offsets.
    code.reserve(std::min<std::size_t>(reader.remaining(), 64));
    while (reader.remaining() != 0) {
      position = static_cast<std::uint16_t>(reader.position());
      offsets.starts[position] = 1;
      offsets.ordinals[position] = static_cast<std::uint16_t>(code.size());
      auto wire = static_cast<Opcode>(*reader.u1());
      auto result = decode_one(wire);
      if (!result)
        return std::unexpected(std::move(result.error()));
    }

    for (const auto& entry : pending) {
      position = entry.source;
      if (!offsets.is_instruction_start(entry.target))
        return fail(ReadErrorKind::InvalidBranchTarget, "branch targets the middle of an instruction or switch padding");
      if (!code.bind_label(offsets.labels[entry.target], CodePosition{code.at(offsets.ordinals[entry.target])}))
        return fail(ReadErrorKind::ResourceLimitExceeded, "exhausted method label handles");
    }
    BytecodeAccess::mark_clean(code);
    return std::move(offsets);
  }

private:
  using DecodeResult = std::expected<void, ReadError>;

  struct Pending {
    std::uint16_t target;
    std::uint16_t source;
  };

  std::unexpected<ReadError> fail(ReadErrorKind kind, std::string_view detail) const {
    return std::unexpected(ReadError{kind, reader.offset() - reader.position() + position,
                                     std::string(path), std::string(detail)});
  }

  DecodeResult append(const InstructionBuilder& value) {
    auto id = code.append(value);
    if (!id)
      return fail(id.error() == CodeEditError::HandleLimit ? ReadErrorKind::ResourceLimitExceeded : ReadErrorKind::InvalidInstruction,
                  "cannot represent the decoded instruction");
    return {};
  }

  DecodeResult append(std::expected<Instruction, CodeEditError> value, EncodingHint hint = EncodingHint::Automatic) {
    if (!value)
      return fail(ReadErrorKind::InvalidInstruction, "invalid instruction operand");
    if (auto error = validate_instruction(*value, version, pool, max_locals))
      return fail(error->constant_pool ? ReadErrorKind::InvalidIndex : ReadErrorKind::InvalidInstruction, error->reason);
    return append(InstructionBuilder{BytecodeAccess::with_hint(*value, hint)});
  }

  DecodeResult constant(Opcode opcode, CpIndex index, EncodingHint hint = EncodingHint::Automatic) {
    if (index.value == 0)
      return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
    return append(Instruction::constant(opcode, index), hint);
  }

  std::expected<LabelId, ReadError> target(std::int32_t displacement) {
    auto offset = static_cast<std::int64_t>(position) + displacement;
    if (offset < 0 || offset >= static_cast<std::int64_t>(offsets.starts.size()))
      return fail(ReadErrorKind::InvalidBranchTarget, "branch target lies outside the code array");
    auto index = static_cast<std::uint16_t>(offset);
    if (offsets.labels.empty())
      offsets.labels.resize(offsets.starts.size() + 1);
    auto& label = offsets.labels[index];
    if (label.scope == 0) {
      auto created = code.make_label();
      if (!created)
        return fail(ReadErrorKind::ResourceLimitExceeded, "exhausted method label handles");
      label = *created;
      pending.push_back({index, position});
    }
    return label;
  }

  DecodeResult decode_wide() {
    auto raw = reader.u1();
    if (!raw)
      return fail(ReadErrorKind::TruncatedInput, "missing opcode after wide prefix");
    auto wire = static_cast<Opcode>(*raw);
    const auto& info = opcode_info(wire);
    if (!info.is_allowed(version.major) || (info.form != OperandForm::Local && info.form != OperandForm::Increment))
      return fail(ReadErrorKind::InvalidOpcode, "wide prefix used with an invalid opcode or class version");
    auto index = reader.u2();
    if (!index)
      return fail(ReadErrorKind::TruncatedInput, "truncated wide local index");
    if (info.form == OperandForm::Local)
      return append(Instruction::local(wire, *index), EncodingHint::Wide);
    auto amount = reader.s2();
    if (!amount)
      return fail(ReadErrorKind::TruncatedInput, "truncated wide iinc increment");
    return append(Instruction::increment(*index, *amount), EncodingHint::Wide);
  }

  DecodeResult decode_switch(bool table) {
    auto padding = reader.take_slice(switch_padding(position));
    if (!padding)
      return fail(ReadErrorKind::TruncatedInput, "truncated switch padding");
    if (version.major < 51 &&
        std::ranges::any_of(*padding, [](auto byte) { return byte != std::byte{0}; }))
      return fail(ReadErrorKind::InvalidInstruction, "switch padding bytes must be zero before class-file version 51");

    auto default_displacement = reader.s4();
    auto low_or_count = reader.s4();
    auto high = table ? reader.s4() : std::optional<std::int32_t>{0};
    if (!default_displacement || !low_or_count || !high)
      return fail(ReadErrorKind::TruncatedInput, "truncated switch header");
    if (table ? *high < *low_or_count : *low_or_count < 0)
      return fail(ReadErrorKind::InvalidInstruction, "invalid switch bounds or case count");

    auto count = table ? static_cast<std::int64_t>(*high) - *low_or_count + 1 : *low_or_count;
    if (count > static_cast<std::int64_t>(reader.remaining() / (table ? 4u : 8u)))
      return fail(ReadErrorKind::TruncatedInput, "switch case table exceeds remaining code");
    auto size = static_cast<std::size_t>(count);
    if (size > options.limits.max_switch_cases)
      return fail(ReadErrorKind::ResourceLimitExceeded, "switch case limit exceeded");

    auto default_target = target(*default_displacement);
    if (!default_target)
      return std::unexpected(std::move(default_target.error()));
    const auto append_switch = [&](const InstructionBuilder& operand) -> DecodeResult {
      auto result = append(operand);
      if (result && !padding->empty())
        BytecodeAccess::set_original_switch_padding(code, code.at(code.size() - 1), *padding);
      return result;
    };
    if (table) {
      std::pmr::vector<LabelId> labels(scratch);
      labels.reserve(size);
      for (std::size_t i = 0; i < size; ++i) {
        auto label = target(*reader.s4());
        if (!label)
          return std::unexpected(std::move(label.error()));
        labels.push_back(*label);
      }
      return append_switch(TableSwitch{*low_or_count, *default_target, labels});
    }
    std::pmr::vector<LookupPair> pairs(scratch);
    pairs.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
      auto key = *reader.s4();
      if (!pairs.empty() && key <= pairs.back().key)
        return fail(ReadErrorKind::InvalidInstruction, "lookupswitch keys are not strictly ascending");
      auto label = target(*reader.s4());
      if (!label)
        return std::unexpected(std::move(label.error()));
      pairs.push_back({key, *label});
    }
    return append_switch(LookupSwitch{*default_target, pairs});
  }

  DecodeResult decode_one(Opcode wire) {
    const auto& info = opcode_info(wire);
    if (!info.is_allowed(version.major))
      return fail(ReadErrorKind::InvalidOpcode, "unrecognized, reserved, or version-inapplicable opcode");
    if (info.wire_width > reader.remaining() + 1)
      return fail(ReadErrorKind::TruncatedInput, "code ends in the middle of an instruction");

    auto opcode = info.logical_opcode;
    switch (info.form) {
      case OperandForm::None:
        return append(Instruction::simple(opcode));
      case OperandForm::SignedByte:
        return append(Instruction::immediate(opcode, *reader.s1()));
      case OperandForm::SignedShort:
        return append(Instruction::immediate(opcode, *reader.s2()));
      case OperandForm::Local:
        return append(Instruction::local(opcode, *reader.u1()), EncodingHint::Explicit);
      case OperandForm::ImplicitLocal:
        return append(Instruction::local(opcode, info.implicit_local), EncodingHint::Implicit);
      case OperandForm::Increment: {
        auto index = *reader.u1();
        auto increment = *reader.s1();
        return append(Instruction::increment(index, increment), EncodingHint::Explicit);
      }
      case OperandForm::ConstantByte:
        return constant(opcode, CpIndex{*reader.u1()}, EncodingHint::Explicit);
      case OperandForm::ConstantShort:
        return constant(opcode, CpIndex{*reader.u2()}, wire == Opcode::LdcW ? EncodingHint::Wide : EncodingHint::Automatic);
      case OperandForm::BranchShort:
      case OperandForm::BranchInt: {
        bool wide = info.form == OperandForm::BranchInt;
        auto label = target(wide ? *reader.s4() : *reader.s2());
        if (!label)
          return std::unexpected(std::move(label.error()));
        return append(Branch{opcode, *label, wide ? EncodingHint::Wide : EncodingHint::Explicit});
      }
      case OperandForm::TableSwitch:
        return decode_switch(true);
      case OperandForm::LookupSwitch:
        return decode_switch(false);
      case OperandForm::InvokeInterface: {
        auto index = CpIndex{*reader.u2()};
        auto count = *reader.u1();
        if (*reader.u1() != 0)
          return fail(ReadErrorKind::InvalidInstruction, "invokeinterface reserved operand byte must be zero");
        if (index.value == 0)
          return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
        return append(Instruction::invoke_interface(index, count));
      }
      case OperandForm::InvokeDynamic: {
        auto index = CpIndex{*reader.u2()};
        if (*reader.u2() != 0)
          return fail(ReadErrorKind::InvalidInstruction, "invokedynamic reserved operand bytes must be zero");
        return constant(opcode, index);
      }
      case OperandForm::NewArray:
        return append(Instruction::new_array(static_cast<ArrayType>(*reader.u1())));
      case OperandForm::MultiANewArray: {
        auto index = CpIndex{*reader.u2()};
        if (index.value == 0)
          return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
        return append(Instruction::multi_array(index, *reader.u1()));
      }
      case OperandForm::WidePrefix:
        return decode_wide();
      case OperandForm::Illegal:
        return fail(ReadErrorKind::InvalidOpcode, "unrecognized or reserved opcode");
    }
    return fail(ReadErrorKind::InvalidOpcode, "unrecognized opcode operand form");
  }

  MethodCode& code;
  BinaryReader reader;
  ClassVersion version;
  const ConstantPool& pool;
  std::uint16_t max_locals;
  const ReadOptions& options;
  std::pmr::memory_resource* scratch;
  std::string_view path;
  DecodedBoundaries offsets;
  std::pmr::vector<Pending> pending;
  std::uint16_t position = 0;
};

std::expected<DecodedBoundaries, ReadError> decode_code(MethodCode& code, std::span<const std::byte> bytes,
                                                        std::size_t absolute_offset, ClassVersion version,
                                                        const ConstantPool& pool, std::uint16_t max_locals,
                                                        const ReadOptions& options, std::pmr::memory_resource* scratch,
                                                        std::string_view path) {
  auto fail = [&](ReadErrorKind kind, std::string_view detail) {
    return std::unexpected(ReadError{kind, absolute_offset, std::string(path), std::string(detail)});
  };
  if (bytes.empty() || bytes.size() > MAX_CODE_SIZE)
    return fail(ReadErrorKind::MalformedEssentialCode, "code length must be between 1 and 65535");
  if (bytes.size() > options.limits.max_code_bytes)
    return fail(ReadErrorKind::ResourceLimitExceeded, "code byte limit exceeded");
  if (bytes.size() > std::numeric_limits<std::size_t>::max() - absolute_offset)
    return fail(ReadErrorKind::MalformedEssentialCode, "code offset overflow");
  if (code.size() != 0 || code.edited())
    return fail(ReadErrorKind::MalformedEssentialCode, "bytecode decoder requires an empty method");

  return CodeDecoder(code, bytes, absolute_offset, version, pool, max_locals, options, scratch, path).run();
}

} // namespace cafe::detail
