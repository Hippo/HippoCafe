#include <array>
#include <gtest/gtest.h>
#include "cafe/analysis/ssa.hpp"
#include "cafe/class_file.hpp"

namespace cafe::analysis {
namespace {

TEST(SsaDump, DistinguishesExceptionSitesAndOrdersPhiInputsByEdge) {
  auto owner = ClassFile::create({61, 0}, "Dump").value();
  auto& pool = owner.constant_pool();
  const auto name = pool.append_utf8("divide").value();
  const auto descriptor = pool.append_utf8("(I)I").value();
  owner.add_method(0x0009, name, descriptor).value();
  AnalysisContext context;
  const auto make_graph = [&](bool reverse) {
    auto graph = create_ssa(owner, 0, context).value();
    const auto block = graph.entry_block();
    const auto handler = graph.add_block().value();
    const auto clause = graph.add_handler(handler, {}).value();
    const auto divisor = graph.parameters().front();
    const SsaType integer{VerificationKind::Integer};
    const SsaType throwable{VerificationKind::Object, "java/lang/Throwable"};
    const auto imm = graph.append_operation(block, Instruction::bipush(-12), {}, integer).value();
    const auto value = graph.operation(imm)->result;
    const auto div = graph.append_operation(block, Instruction::simple(Opcode::IDiv).value(), std::array{value, divisor}, integer).value();
    const auto quotient = graph.operation(div)->result;
    const auto rem = graph.append_operation(block, Instruction::simple(Opcode::IRem).value(), std::array{quotient, divisor}, integer).value();
    graph.append_terminator(block, Opcode::IReturn, std::array{graph.operation(rem)->result}).value();

    const auto first = graph.add_exception_edge(div, clause, throwable).value();
    const auto second = graph.add_exception_edge(rem, clause, throwable).value();
    const auto phi = graph.add_phi(handler, integer).value();
    if (reverse)
      graph.set_phi_input(phi, second, quotient).value();
    graph.set_phi_input(phi, first, value).value();
    if (!reverse)
      graph.set_phi_input(phi, second, quotient).value();
    graph.append_terminator(handler, Opcode::IReturn, std::array{phi}).value();
    return graph;
  };

  auto graph = make_graph(false);
  const auto text = dump_ssa(graph);
  EXPECT_EQ(text, dump_ssa(make_graph(true)));
  EXPECT_EQ(text,
            "ssa method #0\n"
            "v0: int = parameter local[0] on e0\n"
            "v4: java/lang/Throwable = caught on e1\n"
            "v5: java/lang/Throwable = caught on e2\n"
            "b0:\n"
            "  o0: v1: int = bipush -12\n"
            "  o1: v2: int = idiv v1 v0\n"
            "  o2: v3: int = irem v2 v0\n"
            "  o3: ireturn v3\n"
            "b1:\n"
            "  v6: int = phi [e1: v1] [e2: v2]\n"
            "  o4: ireturn v6\n"
            "e0 entry -> b0\n"
            "e1 exceptional b0 -> b1 at o1 handler h0 exception v4\n"
            "e2 exceptional b0 -> b1 at o2 handler h0 exception v5\n"
            "h0 catch all -> b1\n");
}
} // namespace
} // namespace cafe::analysis
