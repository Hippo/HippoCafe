#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

namespace cafe::analysis {
namespace {

TEST(SsaSlotScheduling, LoopPhiSharesParameterAndBackedgeHomes) {
  auto owner = ClassFile::create({61, 0}, "Countdown").value();
  auto& pool = owner.constant_pool();
  const auto index = owner.add_method(0x0009, pool.append_utf8("countdown").value(), pool.append_utf8("(I)I").value()).value();
  auto& attribute = owner.add_code(index, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  const auto head = code.make_label().value();
  const auto exit = code.make_label().value();
  const auto start = code.append(Instruction::iload(0)).value();
  code.bind_label(head, {start}).value();
  (void)code.append(Instruction::branch(Opcode::IfLe, exit).value()).value();
  (void)code.append(Instruction::increment(0, -1)).value();
  (void)code.append(Instruction::branch(Opcode::Goto, head).value()).value();
  const auto result = code.append(Instruction::iload(0)).value();
  code.bind_label(exit, {result}).value();
  (void)code.append(Instruction::simple(Opcode::IReturn).value()).value();

  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  ASSERT_EQ(graph->block(graph->entry_block())->phis.size(), 1u);
  auto lowered = lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  EXPECT_EQ(attribute.max_locals(), 1u);
}

TEST(SsaSlotScheduling, PhiCannotOverwriteParameterStillUsedAfterMerge) {
  auto owner = ClassFile::create({61, 0}, "LiveParameter").value();
  auto& pool = owner.constant_pool();
  const auto index = owner.add_method(0x0009, pool.append_utf8("difference").value(), pool.append_utf8("(I)I").value()).value();
  auto& attribute = owner.add_code(index, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  const auto other = code.make_label().value();
  const auto join = code.make_label().value();
  (void)code.append(Instruction::iload(0)).value();
  (void)code.append(Instruction::branch(Opcode::IfEq, other).value()).value();
  (void)code.append(Instruction::iload(0)).value();
  (void)code.append(Instruction::branch(Opcode::Goto, join).value()).value();
  const auto fallback = code.append(Instruction::simple(Opcode::IConst1).value()).value();
  code.bind_label(other, {fallback}).value();
  const auto compare = code.append(Instruction::iload(0)).value();
  code.bind_label(join, {compare}).value();
  (void)code.append(Instruction::simple(Opcode::ISub).value()).value();
  (void)code.append(Instruction::simple(Opcode::IReturn).value()).value();

  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  EXPECT_EQ(attribute.max_locals(), 2u);
  for (const auto& entry : code.instructions())
    if (entry.instruction.opcode() == Opcode::IStore) {
      EXPECT_NE(entry.instruction.local_index().value(), 0u);
    }
}

TEST(SsaStackScheduling, AccumulatorSurvivesInterveningLocalLoadAnchorsWithoutSpilling) {
  auto owner = ClassFile::create({61, 0}, "SsaScheduling").value();
  auto& pool = owner.constant_pool();
  const auto index = owner.add_method(0x0009, pool.append_utf8("accumulate").value(),
                                      pool.append_utf8("(II)I").value())
                         .value();
  auto& attribute = owner.add_code(index, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  (void)code.append(Instruction::iload(0)).value();
  for (int i = 0; i < 64; ++i) {
    (void)code.append(Instruction::iload(1)).value();
    (void)code.append(Instruction::simple(Opcode::IAdd).value()).value();
  }
  (void)code.append(Instruction::simple(Opcode::IReturn).value()).value();
  const auto source_size = code.size();

  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  EXPECT_EQ(attribute.max_locals(), 2u);
  EXPECT_EQ(code.size(), source_size);
  EXPECT_TRUE(write_class(owner));
}

} // namespace
} // namespace cafe::analysis
