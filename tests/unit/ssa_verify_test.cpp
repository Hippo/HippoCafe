#include <array>

#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <cafe/class_file.hpp>
#include <cafe/io/class_writer.hpp>

namespace cafe::analysis {
namespace {

/** An authored method without Code; verify_ssa never mutates this model. */
struct Subject {
  ClassFile owner = ClassFile::create({61, 0}, "SsaVerifierCase").value();
  AnalysisContext context;
  std::size_t index;
  SsaMethod graph;

  Subject(std::string_view name, std::string_view descriptor, bool instance = false) :
      index(owner.add_method(instance ? 0x0001 : 0x0009, owner.constant_pool().append_utf8(name).value(),
                             owner.constant_pool().append_utf8(descriptor).value())
                .value()),
      graph(create_ssa(owner, index, context).value()) {
  }
};

TEST(SsaVerifier, RejectsSymbolicArgumentTypeWithoutPublishingConstantsOrCode) {
  Subject subject("test", "(F)V");
  const auto original_size = subject.owner.constant_pool().size();
  MethodInvocation invoke{Opcode::InvokeStatic, "SsaVerifierCase", "consume", "(I)V"};
  const auto call = subject.graph.append_symbolic(subject.graph.entry_block(), invoke, subject.graph.parameters()).value();
  (void)subject.graph.append_terminator(subject.graph.entry_block(), Opcode::Return).value();
  auto result = verify_ssa(subject.owner, subject.graph, subject.context);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, SsaErrorKind::TypeMismatch);
  EXPECT_EQ(result.error().operation, call);
  EXPECT_EQ(subject.owner.constant_pool().size(), original_size);
  EXPECT_FALSE(subject.owner.methods()[subject.index].code());
}

TEST(SsaVerifier, RejectsPhiUseAfterConstructorConsumesAllocation) {
  Subject subject("allocate", "()V");
  const auto entry = subject.graph.entry_block();
  const auto next = subject.graph.add_block().value();
  const auto self = subject.owner.this_class();
  const auto fresh = subject.graph.append_operation(entry, Instruction::constant(Opcode::New, self).value(),
                                                    {}, SsaType{VerificationKind::Uninitialized, "SsaVerifierCase"})
                         .value();
  const auto raw = subject.graph.operation(fresh)->result;
  const auto ctor = MethodInvocation{Opcode::InvokeSpecial, "SsaVerifierCase", "<init>", "()V"};
  (void)subject.graph.append_symbolic(entry, ctor, std::array{raw}).value();
  (void)subject.graph.append_terminator(entry, Opcode::Goto).value();
  const auto edge = subject.graph.add_edge(entry, next, SsaEdgeKind::Unconditional).value();
  const auto phi = subject.graph.add_phi(next, subject.graph.value(raw)->type).value();
  (void)subject.graph.set_phi_input(phi, edge, raw).value();
  (void)subject.graph.append_terminator(next, Opcode::Return).value();
  auto result = verify_ssa(subject.owner, subject.graph, subject.context);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, SsaErrorKind::TypeMismatch);
  EXPECT_EQ(result.error().edge, edge);
}

TEST(SsaVerifier, ConstructorCannotReturnThroughBranchBypassingInitialization) {
  Subject subject("<init>", "(I)V", true);
  const auto entry = subject.graph.entry_block(), init = subject.graph.add_block().value(), end = subject.graph.add_block().value();
  (void)subject.graph.append_terminator(entry, Opcode::IfEq, std::array{subject.graph.parameters()[1]}).value();
  (void)subject.graph.add_edge(entry, init, SsaEdgeKind::Conditional).value();
  (void)subject.graph.add_edge(entry, end, SsaEdgeKind::Fallthrough).value();
  (void)subject.graph.append_symbolic(init, MethodInvocation{Opcode::InvokeSpecial, "SsaVerifierCase", "<init>", "()V"},
                                      std::array{subject.graph.parameters()[0]})
      .value();
  (void)subject.graph.append_terminator(init, Opcode::Goto).value();
  (void)subject.graph.add_edge(init, end, SsaEdgeKind::Unconditional).value();
  const auto ret = subject.graph.append_terminator(end, Opcode::Return).value();
  auto result = verify_ssa(subject.owner, subject.graph, subject.context);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, SsaErrorKind::TypeMismatch);
  EXPECT_EQ(result.error().operation, ret);
}

TEST(SsaVerifier, RejectsTableSwitchWithHolesEvenWhenEveryArmHasSameDestination) {
  Subject subject("choose", "(I)V");
  const auto entry = subject.graph.entry_block(), end = subject.graph.add_block().value();
  const auto sw = subject.graph.append_terminator(entry, Opcode::TableSwitch, subject.graph.parameters()).value();
  (void)subject.graph.add_edge(entry, end, SsaEdgeKind::SwitchDefault).value();
  (void)subject.graph.add_edge(entry, end, SsaEdgeKind::SwitchCase, 1).value();
  (void)subject.graph.add_edge(entry, end, SsaEdgeKind::SwitchCase, 3).value();
  (void)subject.graph.append_terminator(end, Opcode::Return).value();
  auto result = verify_ssa(subject.owner, subject.graph, subject.context);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, SsaErrorKind::InvalidGraph);
  EXPECT_EQ(result.error().operation, sw);
}

TEST(SsaVerifier, RejectsUseOfReceiverOnItsFailedInitializationEdge) {
  Subject subject("build", "()V");
  const auto entry = subject.graph.entry_block();
  const auto handler = subject.graph.add_block().value();
  const auto allocated = subject.graph.append_operation(entry, Instruction::constant(Opcode::New, subject.owner.this_class()).value(), {},
                                                        SsaType{VerificationKind::Uninitialized, "SsaVerifierCase"})
                             .value();
  const auto raw = subject.graph.operation(allocated)->result;
  const auto init = subject.graph.append_symbolic(entry, MethodInvocation{Opcode::InvokeSpecial, "SsaVerifierCase", "<init>", "()V"},
                                                  std::array{raw})
                        .value();
  (void)subject.graph.append_terminator(entry, Opcode::Return).value();
  const auto clause = subject.graph.add_handler(handler, {}).value();
  const auto edge = subject.graph.add_exception_edge(init, clause, {VerificationKind::Object, "java/lang/Throwable"}).value();
  const auto alias = subject.graph.add_phi(handler, subject.graph.value(raw)->type).value();
  (void)subject.graph.set_phi_input(alias, edge, raw).value();
  (void)subject.graph.append_terminator(handler, Opcode::AThrow, std::array{subject.graph.edge(edge)->exception}).value();
  const auto rejected = verify_ssa(subject.owner, subject.graph, subject.context);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, SsaErrorKind::TypeMismatch);
  EXPECT_EQ(rejected.error().edge, edge);
}

TEST(SsaVerifier, CountsPhysicalWidthOfVerifierOnlyStackInputs) {
  Subject subject("deadStack", "()V");
  (void)subject.graph.append_terminator(subject.graph.entry_block(), Opcode::Return).value();
  const auto dead = subject.graph.add_block().value();
  const auto root = subject.graph.add_dead_root(dead).value();
  (void)subject.graph.add_root_input(root, SsaValueKind::RootStack, 0, {VerificationKind::Long}).value();
  (void)subject.graph.add_root_input(root, SsaValueKind::RootStack, 1, {VerificationKind::Double}).value();
  (void)subject.graph.append_terminator(dead, Opcode::Return).value();
  AnalysisLimits limits;
  limits.max_stack_slots = 3;
  AnalysisContext small(subject.context.hierarchy(), limits);
  const auto rejected = verify_ssa(subject.owner, subject.graph, small);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, SsaErrorKind::ResourceLimitExceeded);
  ASSERT_TRUE(verify_ssa(subject.owner, subject.graph, subject.context));
}

TEST(SsaVerifier, MakingADeadBlockExecutableRequiresARealValueOnTheNewIncomingEdge) {
  Subject subject("value", "(I)I");
  auto& graph = subject.graph;
  const auto entry = graph.entry_block();
  const auto ret = graph.append_terminator(entry, Opcode::IReturn, graph.parameters()).value();
  const auto dead = graph.add_block().value();
  const auto root = graph.add_dead_root(dead).value();
  const SsaType integer{VerificationKind::Integer};
  const auto incoming_stack = graph.add_root_input(root, SsaValueKind::RootStack, 0, integer).value();
  const auto dead_ret = graph.append_terminator(dead, Opcode::IReturn, std::array{incoming_stack}).value();
  ASSERT_TRUE(verify_ssa(subject.owner, graph, subject.context));
  graph.erase_operation(ret).value();
  (void)graph.append_terminator(entry, Opcode::Goto).value();
  const auto live = graph.add_edge(entry, dead, SsaEdgeKind::Unconditional).value();
  const auto invalid = verify_ssa(subject.owner, graph, subject.context);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().operation, dead_ret);
  const auto phi = graph.add_phi(dead, integer).value();
  graph.set_phi_input(phi, root, incoming_stack).value();
  graph.set_phi_input(phi, live, graph.parameters().front()).value();
  graph.set_input(dead_ret, 0, phi).value();
  const auto lowered = lower_ssa(subject.owner, graph, subject.context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  const auto written = write_class(subject.owner);
  ASSERT_TRUE(written) << written.error().detail;
}

} // namespace
} // namespace cafe::analysis
