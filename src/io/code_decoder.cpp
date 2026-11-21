/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

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

/**
 * @brief Bounded code-array decoder with forward-label fixups and original boundary tracking. It
 * writes into a temporary empty MethodCode owned by the caller.
 * @ingroup implementation
 */
class CodeDecoder {
public:
  /**
   * @brief Initialize bounded code-array decoder with forward-label fixups and original boundary
   * tracking
   * @param code Decoded instruction sequence or associated Code attribute.
   * @param bytes Code byte sequence borrowed for this decoder's lifetime.
   * @param base_offset Absolute byte origin of this code array.
   * @param version Exact JVM class-file major and minor version.
   * @param pool Owning class constant pool used for reference validation.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @param options Operation-specific parsing, emission, or entry policy.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   * @param path Borrowed structural path included in diagnostics.
   */
  CodeDecoder(MethodCode& code, std::span<const std::byte> bytes, std::size_t base_offset,
              ClassVersion version, const ConstantPool& pool, std::uint16_t max_locals,
              const ReadOptions& options, std::pmr::memory_resource* scratch, std::string_view path) :
      code(code), reader(bytes, base_offset), version(version), pool(pool), max_locals(max_locals),
      options(options), scratch(scratch), path(path), offsets(bytes.size(), BytecodeAccess::scope(code), scratch), pending(scratch) {
  }

  /**
   * @brief Scan instructions, then resolve forward branch labels against validated boundaries.
   * @return The original boundary map, or a structured read error.
   */
  // The loop checks remaining() before reading its one-byte opcode.
  std::expected<DecodedBoundaries, ReadError> run() {
    // Code size bounds the number of decoded instructions and target offsets.
    code.reserve(std::min<std::size_t>(reader.remaining(), 64));
    while (reader.remaining() != 0) {
      position = static_cast<std::uint16_t>(reader.position());
      offsets.starts[position] = 1;
      offsets.ordinals[position] = static_cast<std::uint16_t>(code.size());
      auto wire = static_cast<Opcode>(*reader.u1()); // NOLINT(bugprone-unchecked-optional-access)
      auto result = decode_one(wire);
      if (!result)
        return std::unexpected(std::move(result.error()));
    }

    for (const auto& entry : pending) {
      position = entry.source;
      if (!offsets.is_instruction_start(entry.target))
        return fail(ReadErrorKind::InvalidBranchTarget, "branch targets the middle of an instruction or switch padding");
      if (!BytecodeAccess::bind_initial_label(code, offsets.labels[entry.target], CodePosition{code.at(offsets.ordinals[entry.target])}))
        return fail(ReadErrorKind::ResourceLimitExceeded, "exhausted method label handles");
    }
    BytecodeAccess::mark_clean(code);
    return std::move(offsets);
  }

private:
  /**
   * @brief Successful instruction decode or a structured ReadError.
   */
  using DecodeResult = std::expected<void, ReadError>;

  /**
   * @brief Forward branch destination requiring label binding after the bytecode scan completes.
   * @ingroup implementation
   */
  struct Pending {
    std::uint16_t target; ///< Absolute target byte offset within the original code array.
    std::uint16_t source; ///< Byte offset of the branch instruction for diagnostics.
  };

  /**
   * @brief Attach the current opcode offset and method path to a decoding failure.
   * @param kind Payload or error discriminator.
   * @param detail Explanation copied into the structured error.
   * @return An unexpected ReadError.
   */
  std::unexpected<ReadError> fail(ReadErrorKind kind, std::string_view detail) const {
    return std::unexpected(ReadError{kind, reader.offset() - reader.position() + position,
                                     std::string(path), std::string(detail)});
  }

  /**
   * @brief Validate and append a decoded builder while translating edit failures to read failures.
   * @param value Instruction descriptor copied into the method.
   * @return Success or an instruction/operand/handle-limit error.
   */
  DecodeResult append(const InstructionBuilder& value) {
    auto id = code.append(value);
    if (!id)
      return fail(id.error() == CodeEditError::HandleLimit ? ReadErrorKind::ResourceLimitExceeded : ReadErrorKind::InvalidInstruction,
                  "cannot represent the decoded instruction");
    return {};
  }

  /**
   * @brief Validate and append a decoded builder while translating edit failures to read failures.
   * @param value Factory result whose error is translated, or whose instruction is appended.
   * @param hint Preferred wire-encoding form.
   * @return Success or an instruction/operand/handle-limit error.
   */
  DecodeResult append(std::expected<Instruction, CodeEditError> value, EncodingHint hint = EncodingHint::Automatic) {
    if (!value)
      return fail(ReadErrorKind::InvalidInstruction, "invalid instruction operand");
    if (auto error = validate_instruction(*value, version, pool, max_locals))
      return fail(error->constant_pool ? ReadErrorKind::InvalidIndex : ReadErrorKind::InvalidInstruction, error->reason);
    return append(InstructionBuilder{BytecodeAccess::with_hint(*value, hint)});
  }

  /**
   * @brief Build and append a constant-bearing instruction with the selected encoding hint.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @param hint Preferred wire-encoding form.
   * @return Success or an invalid constant operand/read error.
   */
  DecodeResult constant(Opcode opcode, CpIndex index, EncodingHint hint = EncodingHint::Automatic) {
    if (index.value == 0)
      return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
    return append(Instruction::constant(opcode, index), hint);
  }

  /**
   * @brief Create or reuse a forward destination label for a relative branch offset.
   * @param displacement Signed branch displacement relative to the opcode byte.
   * @return A scoped label, or an out-of-code target error.
   */
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

  /**
   * @brief Decode only the legal wide-prefixed local and iinc forms.
   * @return Success or an invalid/truncated instruction error.
   */
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

  /**
   * @brief Decode aligned dense/sparse switch operands with bounded case counts.
   * @param table Whether the switch is dense tableswitch rather than sparse lookupswitch.
   * @return Success or a range/key/target/resource error.
   */
  // The complete switch table length is checked before the fixed-width case reads.
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
        auto label = target(*reader.s4()); // NOLINT(bugprone-unchecked-optional-access)
        if (!label)
          return std::unexpected(std::move(label.error()));
        labels.push_back(*label);
      }
      return append_switch(TableSwitch{*low_or_count, *default_target, labels});
    }
    std::pmr::vector<LookupPair> pairs(scratch);
    pairs.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
      auto key = *reader.s4(); // NOLINT(bugprone-unchecked-optional-access)
      if (!pairs.empty() && key <= pairs.back().key)
        return fail(ReadErrorKind::InvalidInstruction, "lookupswitch keys are not strictly ascending");
      auto label = target(*reader.s4()); // NOLINT(bugprone-unchecked-optional-access)
      if (!label)
        return std::unexpected(std::move(label.error()));
      pairs.push_back({key, *label});
    }
    return append_switch(LookupSwitch{*default_target, pairs});
  }

  /**
   * @brief Check opcode version and fixed width before reading its operands.
   * @param wire Original opcode byte before logical normalization.
   * @return Success or a structured opcode/operand/framing error.
   */
  // The wire width is checked against the bounded reader before fixed-width operands are read.
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
        return append(Instruction::immediate(opcode, *reader.s1())); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::SignedShort:
        return append(Instruction::immediate(opcode, *reader.s2())); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::Local:
        return append(Instruction::local(opcode, *reader.u1()), EncodingHint::Explicit); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::ImplicitLocal:
        return append(Instruction::local(opcode, info.implicit_local), EncodingHint::Implicit);
      case OperandForm::Increment: {
        auto index = *reader.u1(); // NOLINT(bugprone-unchecked-optional-access)
        auto increment = *reader.s1(); // NOLINT(bugprone-unchecked-optional-access)
        return append(Instruction::increment(index, increment), EncodingHint::Explicit);
      }
      case OperandForm::ConstantByte:
        return constant(opcode, CpIndex{*reader.u1()}, EncodingHint::Explicit); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::ConstantShort:
        return constant(opcode, CpIndex{*reader.u2()}, wire == Opcode::LdcW ? EncodingHint::Wide : EncodingHint::Automatic); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::BranchShort:
      case OperandForm::BranchInt: {
        bool wide = info.form == OperandForm::BranchInt;
        auto label = target(wide ? *reader.s4() : *reader.s2()); // NOLINT(bugprone-unchecked-optional-access)
        if (!label)
          return std::unexpected(std::move(label.error()));
        return append(Branch{opcode, *label, wide ? EncodingHint::Wide : EncodingHint::Explicit});
      }
      case OperandForm::TableSwitch:
        return decode_switch(true);
      case OperandForm::LookupSwitch:
        return decode_switch(false);
      case OperandForm::InvokeInterface: {
        auto index = CpIndex{*reader.u2()}; // NOLINT(bugprone-unchecked-optional-access)
        auto count = *reader.u1(); // NOLINT(bugprone-unchecked-optional-access)
        if (*reader.u1() != 0) // NOLINT(bugprone-unchecked-optional-access)
          return fail(ReadErrorKind::InvalidInstruction, "invokeinterface reserved operand byte must be zero");
        if (index.value == 0)
          return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
        return append(Instruction::invoke_interface(index, count));
      }
      case OperandForm::InvokeDynamic: {
        auto index = CpIndex{*reader.u2()}; // NOLINT(bugprone-unchecked-optional-access)
        if (*reader.u2() != 0) // NOLINT(bugprone-unchecked-optional-access)
          return fail(ReadErrorKind::InvalidInstruction, "invokedynamic reserved operand bytes must be zero");
        return constant(opcode, index);
      }
      case OperandForm::NewArray:
        return append(Instruction::new_array(static_cast<ArrayType>(*reader.u1()))); // NOLINT(bugprone-unchecked-optional-access)
      case OperandForm::MultiANewArray: {
        auto index = CpIndex{*reader.u2()}; // NOLINT(bugprone-unchecked-optional-access)
        if (index.value == 0)
          return fail(ReadErrorKind::InvalidIndex, "constant pool index must be nonzero");
        return append(Instruction::multi_array(index, *reader.u1())); // NOLINT(bugprone-unchecked-optional-access)
      }
      case OperandForm::WidePrefix:
        return decode_wide();
      case OperandForm::Illegal:
        return fail(ReadErrorKind::InvalidOpcode, "unrecognized or reserved opcode");
    }
    return fail(ReadErrorKind::InvalidOpcode, "unrecognized opcode operand form");
  }

  MethodCode& code; ///< Borrowed destination method receiving decoded instructions.
  BinaryReader reader; ///< Bounded cursor borrowing the original code bytes.
  ClassVersion version; ///< Exact class-file major and minor version.
  const ConstantPool& pool; ///< Borrowed owning class constant pool used for lookup and validation.
  std::uint16_t max_locals; ///< Declared local-slot limit used for operand and metadata validation.
  const ReadOptions& options; ///< Borrowed operation policy, which must outlive the internal codec.
  std::pmr::memory_resource* scratch; ///< Borrowed temporary allocator for work buffers and interned names.
  std::string_view path; ///< Borrowed structural path used in diagnostics.
  DecodedBoundaries offsets; ///< Instruction starts, ordinals, and lazily materialized labels for the input code.
  std::pmr::vector<Pending> pending; ///< Forward branch-label bindings resolved after the initial instruction scan.
  std::uint16_t position = 0; ///< Byte offset of the current opcode within the code array.
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
