#include <array>
#include <string>

#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

namespace cafe::analysis {
namespace {

std::size_t method(ClassFile& owner, std::string_view descriptor) {
  auto& pool = owner.constant_pool();
  const auto name = pool.append_utf8("test").value();
  const auto type = pool.append_utf8(descriptor).value();
  const auto index = owner.add_method(0x0009, name, type).value();
  std::ignore = owner.add_code(index, pool.append_utf8("Code").value()).value();
  return index;
}

const SsaOperation* find_origin(const SsaMethod& graph, InstructionId source) {
  for (const auto& block : graph.blocks())
    for (const auto id : block.operations) {
      const auto* op = graph.operation(id);
      if (op && op->origin == source)
        return op;
    }
  return nullptr;
}

TEST(SsaLift, PreservesEveryStackShuffleByIdentityAndCategory) {
  struct Shape {
    Opcode op;
    std::string_view before, after;
  };
  const std::array cases{
      Shape{Opcode::Pop, "II", "0"}, Shape{Opcode::Pop2, "III", "0"}, Shape{Opcode::Pop2, "IJ", "0"},
      Shape{Opcode::Dup, "I", "00"}, Shape{Opcode::Swap, "II", "10"}, Shape{Opcode::DupX1, "II", "101"},
      Shape{Opcode::DupX2, "III", "2012"}, Shape{Opcode::DupX2, "JI", "101"},
      Shape{Opcode::Dup2, "II", "0101"}, Shape{Opcode::Dup2, "J", "00"},
      Shape{Opcode::Dup2X1, "III", "12012"}, Shape{Opcode::Dup2X1, "IJ", "101"},
      Shape{Opcode::Dup2X2, "IIII", "230123"}, Shape{Opcode::Dup2X2, "JII", "12012"},
      Shape{Opcode::Dup2X2, "IIJ", "2012"}, Shape{Opcode::Dup2X2, "JJ", "101"}};
  for (const auto& shape : cases) {
    SCOPED_TRACE(std::string(shape.before) + " -> " + std::string(shape.after));
    auto owner = ClassFile::create({61, 0}, "Shuffle").value();
    const auto index = method(owner, "(" + std::string(shape.before) + ")V");
    auto edit = owner.edit_method(index).value();
    unsigned slot = 0;
    for (const auto tag : shape.before) {
      const auto local = static_cast<std::uint16_t>(slot);
      edit.append(tag == 'J' ? Instruction::lload(local) : Instruction::iload(local)).value();
      slot += tag == 'J' ? 2 : 1;
    }
    edit.append(Instruction::simple(shape.op).value()).value();
    std::string descriptor = "(";
    for (auto digit : shape.after)
      descriptor += shape.before[static_cast<std::size_t>(digit - '0')];
    descriptor += ")V";
    const auto call = edit.append(MethodInvocation{Opcode::InvokeStatic, "Shuffle", "consume", descriptor}).value();
    edit.append(Instruction::return_void()).value();
    AnalysisContext context;
    auto graph = lift_ssa(owner, index, context);
    ASSERT_TRUE(graph) << graph.error().detail;
    const auto* use = find_origin(*graph, call);
    ASSERT_NE(use, nullptr);
    ASSERT_EQ(use->inputs.size(), shape.after.size());
    for (std::size_t i = 0; i < use->inputs.size(); ++i)
      EXPECT_EQ(use->inputs[i], graph->parameters()[static_cast<std::size_t>(shape.after[i] - '0')]);
  }
}

TEST(SsaLift, HandlerPhiReadsLocalBeforeEachProtectedInstruction) {
  auto owner = ClassFile::create({61, 0}, "Protected").value();
  const auto index = method(owner, "(I)I");
  auto* body = owner.edit_code(index);
  auto& code = body->code();
  const auto integer_return = Instruction::simple(Opcode::IReturn).value();
  const auto ids = code.append(std::array<InstructionBuilder, 8>{
                                   Instruction::bipush(7), Instruction::istore(0), Instruction::nop(), Instruction::iload(0),
                                   integer_return, Instruction::pop(), Instruction::iload(0), integer_return})
                       .value();
  const auto label = [&](InstructionId id) {
    auto l = code.make_label().value();
    code.bind_label(l, {id}).value();
    return l;
  };
  body->set_exception_handlers(std::array{ExceptionHandler{label(ids[0]), label(ids[3]), label(ids[5]), {}}});
  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* handler_return = find_origin(*graph, ids[7]);
  ASSERT_NE(handler_return, nullptr);
  ASSERT_EQ(handler_return->inputs.size(), 1u);
  const auto* phi = graph->value(handler_return->inputs.front());
  ASSERT_NE(phi, nullptr);
  ASSERT_EQ(phi->kind, SsaValueKind::Phi);
  ASSERT_EQ(phi->incoming.size(), 3u);
  const auto* number = find_origin(*graph, ids[0]);
  ASSERT_NE(number, nullptr);
  for (std::size_t i = 0; i < 3; ++i) {
    const auto* edge = graph->edge(phi->incoming[i].edge);
    ASSERT_NE(edge, nullptr);
    ASSERT_EQ(edge->kind, SsaEdgeKind::Exceptional);
    EXPECT_EQ(graph->operation(edge->site)->origin, ids[i]);
    EXPECT_EQ(phi->incoming[i].value, i < 2 ? graph->parameters().front() : number->result);
  }
}

TEST(SsaLift, DeclaredFrameCanForgetWideValueBelowConsumedInt) {
  auto owner = ClassFile::create({61, 0}, "ForgetWide").value();
  const auto index = method(owner, "(JI)I");
  auto* body = owner.edit_code(index);
  const auto ids = body->code().append(std::array<InstructionBuilder, 3>{
                                           Instruction::lload(0), Instruction::iload(2), Instruction::simple(Opcode::IReturn).value()})
                       .value();
  // At offset 2 the stack is [long,int], assignable to [Top,Top,int].
  const unsigned char data[] = {0, 1, 255, 0, 2, 0, 2, 4, 1, 0, 3, 0, 0, 1};
  std::vector<std::byte> bytes;
  for (const auto x : data)
    bytes.push_back(static_cast<std::byte>(x));
  std::vector<Attribute> attributes;
  attributes.push_back(Attribute::raw(owner.constant_pool().append_utf8("StackMapTable").value(), std::move(bytes)));
  body->supply_metadata(3, 3, {}, std::move(attributes));
  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* ret = find_origin(*graph, ids[2]);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->inputs.size(), 1u);
  EXPECT_EQ(ret->inputs[0], graph->parameters()[1]);
}

TEST(SsaLift, MethodEntryBackedgeIsDistinctFromInvocation) {
  auto owner = ClassFile::create({61, 0}, "EntryLoop").value();
  const auto index = method(owner, "(I)I");
  auto& code = owner.edit_code(index)->code();
  const auto start = code.make_label().value();
  const auto ids = code.append(std::array<InstructionBuilder, 5>{
                                   Instruction::increment(0, -1), Instruction::iload(0), Instruction::branch(Opcode::IfGt, start).value(),
                                   Instruction::iload(0), Instruction::simple(Opcode::IReturn).value()})
                       .value();
  code.bind_label(start, {ids[0]}).value();
  AnalysisContext context;
  auto graph = lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* update = find_origin(*graph, ids.front());
  ASSERT_NE(update, nullptr);
  ASSERT_EQ(update->inputs.size(), 1u);
  const auto* phi = graph->value(update->inputs.front());
  ASSERT_NE(phi, nullptr);
  ASSERT_EQ(phi->kind, SsaValueKind::Phi);
  ASSERT_EQ(phi->incoming.size(), 2u);
  for (const auto& arg : phi->incoming)
    EXPECT_EQ(arg.value, arg.edge == graph->entry_edge() ? graph->parameters().front() : update->result);
}

} // namespace
} // namespace cafe::analysis
