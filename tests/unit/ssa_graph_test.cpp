#include <array>
#include <gtest/gtest.h>
#include "cafe/analysis/ssa.hpp"
#include "cafe/class_file.hpp"

namespace cafe::analysis {
namespace {

ClassFile owner_with_declaration() {
  auto owner = ClassFile::create({61, 0}, "SsaGraph").value();
  auto& pool = owner.constant_pool();
  const auto name = pool.append_utf8("apply").value();
  const auto descriptor = pool.append_utf8("(JDLjava/lang/Object;)V").value();
  owner.add_method(0x0001, name, descriptor).value();
  return owner;
}

TEST(SsaGraph, CopiesSymbolicOperandsIncludingCyclicBootstrapArguments) {
  auto owner = owner_with_declaration();
  AnalysisContext context;
  auto graph = create_ssa(owner, 0, context).value();
  const auto end = graph.append_terminator(graph.entry_block(), Opcode::Return).value();
  const auto before = owner.constant_pool().size();
  SsaOperationId inserted;
  {
    std::string name = "nested";
    std::string owner_name = "SsaGraph";
    std::array<BootstrapArgument, 2> args;
    MethodHandle handle{MethodHandleKind::InvokeStatic, owner_name, "bootstrap", "()V"};
    Bootstrap bootstrap{handle, args};
    args[0].value = std::string_view(name);
    args[1].value = DynamicConstant{name, "I", bootstrap};
    const DynamicInvocation symbol{"call", "()V", bootstrap};
    inserted = graph.insert_symbolic_before(end, symbol).value();
    name.assign("mutated");
    owner_name.clear();
  }
  const auto& op = *graph.operation(inserted);
  ASSERT_EQ(op.opcode, Opcode::InvokeDynamic);
  ASSERT_TRUE(op.symbolic);
  const auto& symbol = std::get<DynamicInvocation>(*op.symbolic);
  ASSERT_EQ(symbol.bootstrap.handle.owner, "SsaGraph");
  ASSERT_EQ(symbol.bootstrap.arguments.size(), 2U);
  EXPECT_EQ(std::get<std::string_view>(symbol.bootstrap.arguments[0].value), "nested");
  const auto& nested = std::get<DynamicConstant>(symbol.bootstrap.arguments[1].value);
  EXPECT_EQ(nested.name, "nested");
  EXPECT_EQ(nested.bootstrap.arguments.data(), symbol.bootstrap.arguments.data());
  ASSERT_EQ(graph.blocks()[0].operations.size(), 2U);
  EXPECT_EQ(graph.blocks()[0].operations.front(), inserted);
  EXPECT_EQ(graph.blocks()[0].operations.back(), end);
  EXPECT_EQ(owner.constant_pool().size(), before);
  ASSERT_TRUE(graph.erase_operation(inserted));
  EXPECT_EQ(graph.operation(inserted), nullptr);
}

TEST(SsaGraph, RefusedOperandAllocationPreservesPriorHandlesAndOrder) {
  auto owner = owner_with_declaration();
  AnalysisLimits limits;
  limits.max_memory_bytes = 20000;
  AnalysisContext context(limits);
  auto graph = create_ssa(owner, 0, context).value();
  const auto end = graph.append_terminator(graph.entry_block(), Opcode::Return).value();
  const std::string oversized(80000, 'a');
  const auto rejected = graph.insert_symbolic_before(end, MethodInvocation{Opcode::InvokeStatic, oversized, "call", "()V"});
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, SsaErrorKind::ResourceLimitExceeded);
  ASSERT_EQ(graph.block(graph.entry_block())->operations.size(), 1U);
  EXPECT_EQ(graph.operation(end)->opcode, Opcode::Return);
  const auto retry = graph.insert_symbolic_before(end, MethodInvocation{Opcode::InvokeStatic, "A", "call", "()V"});
  ASSERT_TRUE(retry) << retry.error().detail;
  EXPECT_EQ(graph.block(graph.entry_block())->operations.front(), *retry);
}

TEST(SsaGraph, RetiresHandlesAndMakesCapacityAvailableForLaterEdits) {
  auto owner = owner_with_declaration();
  AnalysisLimits limits;
  limits.max_instructions = 1;
  AnalysisContext context(limits);
  auto graph = create_ssa(owner, 0, context).value();
  const auto original = graph.append_operation(graph.entry_block(), Instruction::nop()).value();
  EXPECT_EQ(graph.append_terminator(graph.entry_block(), Opcode::Return).error().kind, SsaErrorKind::ResourceLimitExceeded);
  ASSERT_TRUE(graph.erase_operation(original));
  const auto replacement = graph.append_terminator(graph.entry_block(), Opcode::Return).value();
  EXPECT_NE(replacement, original);
  EXPECT_EQ(graph.operation(original), nullptr);
  EXPECT_EQ(graph.operation(replacement)->opcode, Opcode::Return);
}

TEST(SsaGraph, EnumeratesParameterSlotsAndVerifierRootInputs) {
  auto owner = owner_with_declaration();
  AnalysisContext context;
  auto graph = create_ssa(owner, 0, context).value();
  const auto parameters = graph.root_inputs(graph.entry_edge()).value();
  ASSERT_EQ(parameters.size(), 4U);
  const std::array<unsigned, 4> slots{0, 1, 3, 5};
  for (std::size_t i = 0; i < slots.size(); ++i) {
    ASSERT_EQ(parameters[i], graph.parameters()[i]);
    EXPECT_EQ(graph.value(parameters[i])->slot, slots[i]);
  }
  const auto block = graph.add_block().value();
  const auto root = graph.add_dead_root(block, true).value();
  EXPECT_TRUE(graph.edge(root)->constructor_uninitialized);
  const auto local = graph.add_root_input(root, SsaValueKind::RootLocal, 50, {VerificationKind::Double}).value();
  const auto stack = graph.add_root_input(root, SsaValueKind::RootStack, 1, {VerificationKind::Null}).value();
  EXPECT_EQ(graph.root_inputs(root).value(), (std::vector<SsaValueId>{local, stack}));
  const auto use = graph.append_terminator(block, Opcode::AThrow, std::array{stack}).value();
  ASSERT_FALSE(graph.erase_edge(root));
  ASSERT_TRUE(graph.erase_operation(use));
  ASSERT_TRUE(graph.erase_edge(root));
  EXPECT_EQ(graph.value(local), nullptr);
  EXPECT_EQ(graph.value(stack), nullptr);
  EXPECT_EQ(graph.edge(root), nullptr);
}
} // namespace
} // namespace cafe::analysis
