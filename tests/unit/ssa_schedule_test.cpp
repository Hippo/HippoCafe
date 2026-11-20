#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

namespace cafe::analysis {
namespace {

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
