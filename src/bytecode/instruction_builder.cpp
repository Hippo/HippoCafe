#include "cafe/bytecode/instruction.hpp"

#include <bit>
#include <utility>

namespace cafe {
namespace {

bool is_constant_form(OperandForm form) {
  return form == OperandForm::ConstantByte || form == OperandForm::ConstantShort ||
         form == OperandForm::InvokeInterface || form == OperandForm::InvokeDynamic ||
         form == OperandForm::MultiANewArray;
}

} // namespace

std::expected<Instruction, CodeEditError> Instruction::simple(Opcode opcode) {
  if (opcode_info(opcode).form != OperandForm::None)
    return std::unexpected(CodeEditError::WrongOperandForm);
  return Instruction(opcode, 0);
}

std::expected<Instruction, CodeEditError> Instruction::immediate(Opcode opcode, std::int32_t value) {
  auto form = opcode_info(opcode).form;
  if (form != OperandForm::SignedByte && form != OperandForm::SignedShort)
    return std::unexpected(CodeEditError::WrongOperandForm);
  if ((form == OperandForm::SignedByte && !std::in_range<std::int8_t>(value)) ||
      (form == OperandForm::SignedShort && !std::in_range<std::int16_t>(value)))
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction(opcode, std::bit_cast<std::uint32_t>(value));
}

std::expected<Instruction, CodeEditError> Instruction::local(Opcode opcode, std::uint16_t index) {
  if (opcode_info(opcode).form != OperandForm::Local)
    return std::unexpected(CodeEditError::WrongOperandForm);
  return Instruction(opcode, index);
}

Instruction Instruction::increment(std::uint16_t index, std::int16_t amount) {
  return Instruction(Opcode::IInc, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(amount)), index);
}

std::expected<Instruction, CodeEditError> Instruction::constant(Opcode opcode, CpIndex index) {
  const auto& info = opcode_info(opcode);
  if (!is_constant_form(info.form) || info.form == OperandForm::InvokeInterface ||
      info.form == OperandForm::MultiANewArray || info.logical_opcode != opcode)
    return std::unexpected(CodeEditError::WrongOperandForm);
  if (index.value == 0)
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction(opcode, index.value);
}

std::expected<Instruction, CodeEditError> Instruction::invoke_interface(CpIndex index, std::uint8_t count) {
  if (index.value == 0 || count == 0)
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction(Opcode::InvokeInterface, index.value, count);
}

std::expected<Instruction, CodeEditError> Instruction::multi_array(CpIndex index, std::uint8_t dimensions) {
  if (index.value == 0 || dimensions == 0)
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction(Opcode::MultiANewArray, index.value, dimensions);
}

std::expected<Instruction, CodeEditError> Instruction::new_array(ArrayType type) {
  auto atype = static_cast<std::uint8_t>(type);
  if (atype < static_cast<std::uint8_t>(ArrayType::Boolean) || atype > static_cast<std::uint8_t>(ArrayType::Long))
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction(Opcode::NewArray, atype);
}

std::expected<Branch, CodeEditError> Instruction::branch(Opcode opcode, LabelId target) {
  if (opcode_info(opcode).form != OperandForm::BranchShort)
    return std::unexpected(CodeEditError::WrongOperandForm);
  return Branch{opcode, target};
}

std::optional<std::int32_t> Instruction::immediate_value() const {
  auto form = opcode_info(operation).form;
  if (form != OperandForm::SignedByte && form != OperandForm::SignedShort)
    return std::nullopt;
  return std::bit_cast<std::int32_t>(operand_bits);
}

std::optional<std::uint16_t> Instruction::local_index() const {
  if (opcode_info(operation).form != OperandForm::Local)
    return std::nullopt;
  return static_cast<std::uint16_t>(operand_bits);
}

std::optional<Increment> Instruction::increment_value() const {
  if (operation != Opcode::IInc)
    return std::nullopt;
  return Increment{secondary, static_cast<std::int16_t>(std::bit_cast<std::int32_t>(operand_bits))};
}

std::optional<CpIndex> Instruction::constant_index() const {
  if (!is_constant_form(opcode_info(operation).form))
    return std::nullopt;
  return CpIndex{static_cast<std::uint16_t>(operand_bits)};
}

std::optional<std::uint8_t> Instruction::interface_count() const {
  if (operation != Opcode::InvokeInterface)
    return std::nullopt;
  return static_cast<std::uint8_t>(secondary);
}

std::optional<std::uint8_t> Instruction::dimensions() const {
  if (operation != Opcode::MultiANewArray)
    return std::nullopt;
  return static_cast<std::uint8_t>(secondary);
}

std::optional<ArrayType> Instruction::array_type() const {
  if (operation != Opcode::NewArray)
    return std::nullopt;
  return static_cast<ArrayType>(operand_bits);
}

} // namespace cafe
