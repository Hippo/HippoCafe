#include "code_encoder.hpp"

#include <utility>

#include "cafe/bytecode/code.hpp"

#include "binary_writer.hpp"
#include "bytecode/code_access.hpp"
#include "code_operands.hpp"

namespace cafe::detail {
namespace {

std::optional<Opcode> inverse_condition(Opcode opcode) {
  if (opcode >= Opcode::IfEq && opcode <= Opcode::IfACmpNe) {
    const auto base = std::to_underlying(Opcode::IfEq);
    return static_cast<Opcode>(base + ((std::to_underlying(opcode) - base) ^ 1));
  }
  if (opcode == Opcode::IfNull)
    return Opcode::IfNonNull;
  if (opcode == Opcode::IfNonNull)
    return Opcode::IfNull;
  return std::nullopt;
}

} // namespace

EncodedCode::EncodedCode(CodeLayout layout, std::pmr::memory_resource* scratch) :
    layout(std::move(layout)), output(scratch), starts(this->layout.offsets.back(), scratch) {
  output.reserve(starts.size());
}

std::expected<EncodedCode, WriteError> encode_code(const MethodCode& code, ClassVersion version,
                                                   const ConstantPool& pool, std::uint16_t max_locals,
                                                   bool preserve_original_encodings,
                                                   std::pmr::memory_resource* scratch, std::string_view path,
                                                   bool expand_conditionals) {
  auto layout = layout_code(code, version, pool, max_locals, preserve_original_encodings, scratch, path, expand_conditionals);
  if (!layout)
    return std::unexpected(std::move(layout.error()));

  EncodedCode encoded(std::move(*layout), scratch);
  BinaryWriter writer(encoded.output);
  const auto write_padding = [&](InstructionId id, std::uint32_t offset) {
    auto count = switch_padding(offset);
    if (preserve_original_encodings && version.major >= 51 && count != 0) {
      auto original = BytecodeAccess::original_switch_padding(code, id);
      if (original.size() == count) {
        writer.bytes(original);
        return;
      }
    }
    for (std::uint32_t i = 0; i < count; ++i)
      writer.u1(0);
  };
  for (std::size_t i = 0; i < code.size(); ++i) {
    auto id = code.at(i);
    auto instruction = *code.get(id);
    auto wire = encoded.layout.encodings[i];
    auto offset = encoded.layout.offsets[i];
    // Layout has already checked label binding, signed widths, and operands.
    const auto displacement = [&](LabelId target) {
      return static_cast<std::int32_t>(static_cast<std::int64_t>(*encoded.offset_of(target)) - offset);
    };

    encoded.starts[offset] = 1;
    if (encoded.layout.expanded(i)) {
      const auto inverse = inverse_condition(wire);
      if (!inverse)
        return std::unexpected(WriteError{WriteErrorKind::InvalidCodeLayout, std::string(path),
                                          "cannot invert the conditional branch"});
      writer.u1(std::to_underlying(*inverse));
      writer.s2(8);
      encoded.starts[offset + 3] = 1;
      writer.u1(std::to_underlying(Opcode::GotoW));
      writer.s4(displacement(*code.branch_target(id)) - 3);
      continue;
    }
    writer.u1(static_cast<std::uint8_t>(wire));
    switch (opcode_info(wire).form) {
      case OperandForm::None:
      case OperandForm::ImplicitLocal:
        break;
      case OperandForm::SignedByte:
        writer.s1(static_cast<std::int8_t>(*instruction.immediate_value()));
        break;
      case OperandForm::SignedShort:
        writer.s2(static_cast<std::int16_t>(*instruction.immediate_value()));
        break;
      case OperandForm::Local:
        writer.u1(static_cast<std::uint8_t>(*instruction.local_index()));
        break;
      case OperandForm::Increment: {
        auto increment = *instruction.increment_value();
        writer.u1(static_cast<std::uint8_t>(increment.local));
        writer.s1(static_cast<std::int8_t>(increment.amount));
        break;
      }
      case OperandForm::ConstantByte:
        writer.u1(static_cast<std::uint8_t>(instruction.constant_index()->value));
        break;
      case OperandForm::ConstantShort:
        writer.u2(instruction.constant_index()->value);
        break;
      case OperandForm::BranchShort:
        writer.s2(static_cast<std::int16_t>(displacement(*code.branch_target(id))));
        break;
      case OperandForm::BranchInt:
        writer.s4(displacement(*code.branch_target(id)));
        break;
      case OperandForm::TableSwitch: {
        auto table = *code.table_switch(*code.switch_id(id));
        write_padding(id, offset);
        writer.s4(displacement(table.default_target));
        writer.s4(table.low);
        writer.s4(static_cast<std::int32_t>(static_cast<std::int64_t>(table.low) + static_cast<std::int64_t>(table.targets.size()) - 1));
        for (auto target : table.targets)
          writer.s4(displacement(target));
        break;
      }
      case OperandForm::LookupSwitch: {
        auto lookup = *code.lookup_switch(*code.switch_id(id));
        write_padding(id, offset);
        writer.s4(displacement(lookup.default_target));
        writer.s4(static_cast<std::int32_t>(lookup.pairs.size()));
        for (const auto& pair : lookup.pairs) {
          writer.s4(pair.key);
          writer.s4(displacement(pair.target));
        }
        break;
      }
      case OperandForm::InvokeInterface:
        writer.u2(instruction.constant_index()->value);
        writer.u1(*instruction.interface_count());
        writer.u1(0);
        break;
      case OperandForm::InvokeDynamic:
        writer.u2(instruction.constant_index()->value);
        writer.u2(0);
        break;
      case OperandForm::NewArray:
        writer.u1(static_cast<std::uint8_t>(*instruction.array_type()));
        break;
      case OperandForm::MultiANewArray:
        writer.u2(instruction.constant_index()->value);
        writer.u1(*instruction.dimensions());
        break;
      case OperandForm::WidePrefix:
        writer.u1(static_cast<std::uint8_t>(instruction.opcode()));
        if (auto increment = instruction.increment_value()) {
          writer.u2(increment->local);
          writer.s2(increment->amount);
        } else {
          writer.u2(*instruction.local_index());
        }
        break;
      case OperandForm::Illegal:
        return std::unexpected(WriteError{WriteErrorKind::InvalidInstruction, std::string(path), "cannot emit illegal opcode"});
    }
  }
  if (writer.size() != encoded.starts.size())
    return std::unexpected(WriteError{WriteErrorKind::InvalidCodeLayout, std::string(path), "encoded widths differ from code layout"});
  return encoded;
}

} // namespace cafe::detail
