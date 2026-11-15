/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup implementation
 */

#include "cafe/bytecode/opcode.hpp"

#include <array>

namespace cafe {
namespace {

/**
 * @brief Build one immutable table for wire forms, logical normalization, local widths, and
 * control flow.
 * @return Metadata for all 256 opcode bytes, including illegal/reserved entries.
 * @ingroup implementation
 */
constexpr auto make_opcode_table() {
  std::array<OpcodeInfo, 256> table{};
  auto entry = [&](Opcode opcode) -> OpcodeInfo& {
    return table[static_cast<std::uint8_t>(opcode)];
  };
  auto form = [&](Opcode opcode, OperandForm operand_form, std::uint8_t width) {
    entry(opcode).form = operand_form;
    entry(opcode).wire_width = width;
  };
  auto range = [](Opcode first, Opcode last, auto apply) {
    for (auto value = static_cast<unsigned>(first); value <= static_cast<unsigned>(last); ++value)
      apply(static_cast<Opcode>(value));
  };

  range(Opcode::Nop, Opcode::JsrW, [&](Opcode opcode) {
    entry(opcode).logical_opcode = opcode;
    form(opcode, OperandForm::None, 1);
  });
  form(Opcode::Bipush, OperandForm::SignedByte, 2);
  form(Opcode::Sipush, OperandForm::SignedShort, 3);
  form(Opcode::Ldc, OperandForm::ConstantByte, 2);
  form(Opcode::LdcW, OperandForm::ConstantShort, 3);
  form(Opcode::Ldc2W, OperandForm::ConstantShort, 3);
  entry(Opcode::LdcW).logical_opcode = Opcode::Ldc;

  auto local_group = [&](Opcode first_explicit, Opcode first_implicit) {
    for (unsigned family = 0; family < 5; ++family) {
      auto explicit_opcode = static_cast<Opcode>(static_cast<unsigned>(first_explicit) + family);
      auto local_width = static_cast<std::uint8_t>(family == 1 || family == 3 ? 2 : 1);
      form(explicit_opcode, OperandForm::Local, 2);
      entry(explicit_opcode).local_width = local_width;
      for (unsigned index = 0; index < 4; ++index) {
        auto implicit_opcode = static_cast<Opcode>(static_cast<unsigned>(first_implicit) + 4 * family + index);
        form(implicit_opcode, OperandForm::ImplicitLocal, 1);
        auto& info = entry(implicit_opcode);
        info.logical_opcode = explicit_opcode;
        info.implicit_local = static_cast<std::uint8_t>(index);
        info.local_width = local_width;
      }
    }
  };
  local_group(Opcode::ILoad, Opcode::ILoad0);
  local_group(Opcode::IStore, Opcode::IStore0);
  form(Opcode::Ret, OperandForm::Local, 2);
  entry(Opcode::Ret).local_width = 1;
  entry(Opcode::Ret).control_flow = ControlFlow::SubroutineReturn;
  form(Opcode::IInc, OperandForm::Increment, 3);
  entry(Opcode::IInc).local_width = 1;

  range(Opcode::IfEq, Opcode::Jsr, [&](Opcode opcode) {
    form(opcode, OperandForm::BranchShort, 3);
    entry(opcode).control_flow = ControlFlow::ConditionalBranch;
  });
  range(Opcode::IfNull, Opcode::IfNonNull, [&](Opcode opcode) {
    form(opcode, OperandForm::BranchShort, 3);
    entry(opcode).control_flow = ControlFlow::ConditionalBranch;
  });
  entry(Opcode::Goto).control_flow = ControlFlow::Jump;
  entry(Opcode::Jsr).control_flow = ControlFlow::SubroutineCall;
  for (auto opcode : {Opcode::GotoW, Opcode::JsrW}) {
    form(opcode, OperandForm::BranchInt, 5);
    auto logical = opcode == Opcode::GotoW ? Opcode::Goto : Opcode::Jsr;
    entry(opcode).logical_opcode = logical;
    entry(opcode).control_flow = entry(logical).control_flow;
  }
  for (auto opcode : {Opcode::Jsr, Opcode::JsrW, Opcode::Ret})
    entry(opcode).maximum_major = 50;

  form(Opcode::TableSwitch, OperandForm::TableSwitch, 0);
  form(Opcode::LookupSwitch, OperandForm::LookupSwitch, 0);
  entry(Opcode::TableSwitch).control_flow = ControlFlow::Switch;
  entry(Opcode::LookupSwitch).control_flow = ControlFlow::Switch;
  range(Opcode::IReturn, Opcode::Return, [&](Opcode opcode) {
    entry(opcode).control_flow = ControlFlow::Return;
  });
  entry(Opcode::AThrow).control_flow = ControlFlow::Throw;

  range(Opcode::GetStatic, Opcode::InvokeStatic, [&](Opcode opcode) {
    form(opcode, OperandForm::ConstantShort, 3);
  });
  for (auto opcode : {Opcode::New, Opcode::ANewArray, Opcode::CheckCast, Opcode::InstanceOf})
    form(opcode, OperandForm::ConstantShort, 3);
  form(Opcode::InvokeInterface, OperandForm::InvokeInterface, 5);
  form(Opcode::InvokeDynamic, OperandForm::InvokeDynamic, 5);
  entry(Opcode::InvokeDynamic).minimum_major = 51;
  form(Opcode::NewArray, OperandForm::NewArray, 2);
  form(Opcode::MultiANewArray, OperandForm::MultiANewArray, 4);
  form(Opcode::Wide, OperandForm::WidePrefix, 0);
  return table;
}

constexpr auto OPCODE_TABLE = make_opcode_table(); ///< Shared immutable metadata for every possible opcode byte.

} // namespace

const OpcodeInfo& opcode_info(Opcode opcode) {
  return OPCODE_TABLE[static_cast<std::uint8_t>(opcode)];
}

} // namespace cafe
