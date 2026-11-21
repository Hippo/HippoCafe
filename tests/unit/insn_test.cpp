#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <cafe/bytecode/insn.hpp>

namespace cafe {
namespace {

TEST(InstructionFactories, NamedInstructionsUseNormalizedOpcodesAndTypedOperands) {
  EXPECT_EQ(std::get<Instruction>(insn::iconst_m1()).opcode(), Opcode::IConstM1);
  EXPECT_EQ(std::get<Instruction>(insn::iaload()).opcode(), Opcode::IALoad);
  EXPECT_EQ(std::get<Instruction>(insn::dup2()).opcode(), Opcode::Dup2);
  EXPECT_EQ(std::get<Instruction>(insn::i2l()).opcode(), Opcode::I2L);
  EXPECT_EQ(std::get<Instruction>(insn::dreturn()).opcode(), Opcode::DReturn);
  EXPECT_EQ(std::get<Instruction>(insn::return_()).opcode(), Opcode::Return);
  EXPECT_EQ(std::get<Instruction>(insn::iload(513)).local_index(), 513);
  EXPECT_EQ(std::get<Instruction>(insn::sipush(-32768)).immediate_value(), -32768);
  EXPECT_EQ(std::get<Instruction>(insn::bipush(-128)).immediate_value(), -128);
  const auto increment = std::get<Instruction>(insn::iinc(257u, -256)).increment_value().value();
  EXPECT_EQ(increment.local, 257);
  EXPECT_EQ(increment.amount, -256);
}

TEST(InstructionFactories, NumericArgumentsFailBeforeImplicitNarrowing) {
  const auto invalid = std::unexpected(CodeEditError::InvalidOperand);
  for (const auto& value : {insn::bipush(-129), insn::bipush(256u), insn::sipush(65536u),
                            insn::iinc(-1, 0), insn::iinc(0, 32768), insn::astore(65536u),
                            insn::ldc(std::numeric_limits<std::uint64_t>::max()), insn::ldc(UINT32_MAX),
                            insn::tableswitch(UINT64_MAX, {}, {}), insn::multianewarray("[[I", 0),
                            insn::multianewarray("[[I", 257), insn::newarray(static_cast<ArrayType>(2))}) {
    EXPECT_EQ(std::get<std::unexpected<CodeEditError>>(value), invalid);
  }
}

TEST(InstructionFactories, ConstantsAndClassOperandsRemainSymbolic) {
  EXPECT_EQ(std::get<std::int32_t>(std::get<ConstantLoad>(insn::ldc(-1)).constant.value), -1);
  EXPECT_EQ(std::get<std::int64_t>(std::get<ConstantLoad>(insn::ldc(42LL)).constant.value), 42);
  EXPECT_EQ(std::get<float>(std::get<ConstantLoad>(insn::ldc(2.5f)).constant.value), 2.5f);
  EXPECT_EQ(std::get<double>(std::get<ConstantLoad>(insn::ldc(-1.0)).constant.value), -1.0);
  EXPECT_EQ(std::get<std::string_view>(std::get<ConstantLoad>(insn::ldc("Hello")).constant.value), "Hello");
  EXPECT_EQ(std::get<ClassConstant>(std::get<ConstantLoad>(insn::ldc(ClassConstant{"I"})).constant.value).name, "I");
  EXPECT_EQ(std::get<DynamicConstant>(insn::ldc(DynamicConstant{"size", "J", {}})).descriptor, "J");
  const auto allocate = std::get<TypeInstruction>(insn::multianewarray("[[I", 2));
  EXPECT_EQ(allocate.name, "[[I");
  EXPECT_EQ(allocate.dimensions, 2);
  EXPECT_EQ(allocate.opcode, Opcode::MultiANewArray);
  EXPECT_EQ(std::get<TypeInstruction>(insn::new_("example/Type")).opcode, Opcode::New);
  EXPECT_EQ(std::get<Instruction>(insn::newarray(ArrayType::Byte)).array_type(), ArrayType::Byte);
  EXPECT_TRUE(std::get<MethodInvocation>(insn::invokespecial("I", "f", "()V", true)).owner_is_interface);
  EXPECT_EQ(std::get<FieldAccess>(insn::putfield("Owner", "field", "J")).opcode, Opcode::PutField);
}

TEST(InstructionFactories, MethodScopedBranchesAndSwitchesKeepTheirTargets) {
  constexpr LabelId target{3, 7};
  const auto branch = std::get<Branch>(insn::goto_(target));
  EXPECT_EQ(branch.opcode, Opcode::Goto);
  EXPECT_EQ(branch.target, target);
  const std::array targets{target};
  const auto table = std::get<TableSwitch>(insn::tableswitch(-4, target, targets));
  EXPECT_EQ(table.low, -4);
  EXPECT_EQ(table.targets.data(), targets.data());
  const std::array pairs{LookupPair{9, target}};
  const auto lookup = std::get<LookupSwitch>(insn::lookupswitch(target, pairs));
  EXPECT_EQ(lookup.pairs.front(), pairs.front());
}

} // namespace
} // namespace cafe
