#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

namespace cafe::analysis {
namespace {

constexpr SsaType INT{VerificationKind::Integer};
constexpr SsaType LONG{VerificationKind::Long};
constexpr ClassVersion MODERN{61, 0};

Instruction simple(Opcode opcode) {
  return Instruction::simple(opcode).value();
}

struct MethodFixture {
  ClassFile owner = ClassFile::create(MODERN, "SsaFixture").value();
  AnalysisContext context;
  std::size_t index;

  explicit MethodFixture(std::string_view descriptor, bool attach_code = true) {
    auto& pool = owner.constant_pool();
    index = owner.add_method(0x0009, pool.append_utf8("test").value(), pool.append_utf8(descriptor).value()).value();
    if (attach_code)
      (void)owner.add_code(index, pool.append_utf8("Code").value()).value();
  }

  MethodCode& code() {
    return owner.edit_code(index)->code();
  }

  SsaResult<SsaMethod> lift() {
    return lift_ssa(owner, index, context);
  }

  CpIndex add_capture(std::string_view signature) {
    auto& pool = owner.constant_pool();
    const auto name = pool.append_utf8("capture").value();
    const auto type = pool.append_utf8(signature).value();
    (void)owner.add_method(0x0109, name, type).value();
    const auto pair = pool.append(CpEntry{CpTag::NameAndType, 0, name, type}).value();
    return pool.append(CpEntry{CpTag::Methodref, 0, owner.this_class(), pair}).value();
  }
};

const SsaOperation* find_operation(const SsaMethod& graph, InstructionId origin) {
  for (const auto& block : graph.blocks())
    for (const auto id : block.operations)
      if (const auto* op = graph.operation(id); op && op->origin == origin)
        return op;
  return nullptr;
}

void expect_writable(MethodFixture& fixture, const SsaMethod& graph) {
  auto verified = verify_ssa(fixture.owner, graph, fixture.context);
  ASSERT_TRUE(verified) << verified.error().detail;
  auto lowered = lower_ssa(fixture.owner, graph, fixture.context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  const auto written = write_class(fixture.owner);
  ASSERT_TRUE(written) << written.error().detail;
  const auto parsed = read_class(written->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto relift = lift_ssa(parsed->class_file, fixture.index, fixture.context);
  ASSERT_TRUE(relift) << relift.error().detail;
}

TEST(SsaConstruction, PreservesValueIdentityThroughEveryLegalStackPermutation) {
  struct Form {
    Opcode opcode;
    std::string_view stack; // JVM types from bottom to top.
    std::string_view order; // Input value positions in the final argument list.
  };
  constexpr std::array forms{
      Form{Opcode::Pop, "I", ""},
      Form{Opcode::Pop2, "II", ""},
      Form{Opcode::Pop2, "J", ""},
      Form{Opcode::Dup, "I", "00"},
      Form{Opcode::DupX1, "II", "101"},
      Form{Opcode::DupX2, "III", "2012"},
      Form{Opcode::DupX2, "JI", "101"},
      Form{Opcode::Dup2, "II", "0101"},
      Form{Opcode::Dup2, "J", "00"},
      Form{Opcode::Dup2X1, "III", "12012"},
      Form{Opcode::Dup2X1, "IJ", "101"},
      Form{Opcode::Dup2X2, "IIII", "230123"},
      Form{Opcode::Dup2X2, "IIJ", "2012"},
      Form{Opcode::Dup2X2, "JII", "12012"},
      Form{Opcode::Dup2X2, "JJ", "101"},
      Form{Opcode::Swap, "II", "10"},
  };
  for (const auto& form : forms) {
    SCOPED_TRACE(::testing::Message() << "opcode " << static_cast<unsigned>(form.opcode) << " stack " << form.stack);
    MethodFixture fixture("()V");
    std::string signature = "(";
    for (const char index : form.order)
      signature += form.stack[static_cast<std::size_t>(index - '0')];
    signature += ")V";
    const auto capture = fixture.add_capture(signature);
    auto& code = fixture.code();
    std::vector<InstructionId> definitions;
    unsigned longs = 0;
    for (const char type : form.stack) {
      const auto instruction = type == 'I' ? Instruction::bipush(static_cast<std::int8_t>(definitions.size() + 7))
                                           : simple(longs++ == 0 ? Opcode::LConst0 : Opcode::LConst1);
      definitions.push_back(code.append(instruction).value());
    }
    (void)code.append(simple(form.opcode)).value();
    const auto call = code.append(Instruction::constant(Opcode::InvokeStatic, capture).value()).value();
    (void)code.append(Instruction::return_void()).value();
    auto graph = fixture.lift();
    ASSERT_TRUE(graph) << graph.error().detail;
    const auto* invoke = find_operation(*graph, call);
    ASSERT_NE(invoke, nullptr);
    ASSERT_EQ(invoke->inputs.size(), form.order.size());
    for (std::size_t i = 0; i < form.order.size(); ++i) {
      const auto input = static_cast<std::size_t>(form.order[i] - '0');
      const auto* producer = find_operation(*graph, definitions[input]);
      ASSERT_NE(producer, nullptr);
      EXPECT_EQ(invoke->inputs[i], producer->result);
    }
    expect_writable(fixture, *graph);
  }
}

TEST(SsaConstruction, PhiInputsDistinguishTheCriticalBranchArmFromFallthrough) {
  MethodFixture fixture("(I)I");
  auto& code = fixture.code();
  const auto join = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 6>{
                             simple(Opcode::IConst1), Instruction::iload(0), Instruction::branch(Opcode::IfEq, join).value(),
                             Instruction::pop(), simple(Opcode::IConst2), simple(Opcode::IReturn)})
                 .value();
  code.bind_label(join, {ids[5]}).value();
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* ret = find_operation(*graph, ids[5]);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->inputs.size(), 1U);
  const auto* merged = graph->value(ret->inputs.front());
  ASSERT_NE(merged, nullptr);
  EXPECT_EQ(merged->kind, SsaValueKind::Phi);
  ASSERT_EQ(merged->incoming.size(), 2U);
  const auto* first = find_operation(*graph, ids[0]);
  const auto* second = find_operation(*graph, ids[4]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  for (const auto [edge_id, value] : merged->incoming) {
    const auto* edge = graph->edge(edge_id);
    ASSERT_NE(edge, nullptr);
    ASSERT_TRUE(edge->kind == SsaEdgeKind::Conditional || edge->kind == SsaEdgeKind::Fallthrough);
    EXPECT_EQ(value, edge->kind == SsaEdgeKind::Conditional ? first->result : second->result);
  }
  auto use_list = graph->uses(merged->id);
  ASSERT_TRUE(use_list);
  ASSERT_TRUE(std::ranges::any_of(*use_list, [&](const SsaUse& use) { return use.operation == ret->id && use.index == 0; }));
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, LoopCarriedStackValuesFormAPairOfCyclicPhiCopies) {
  MethodFixture fixture("(I)I");
  auto& code = fixture.code();
  const auto head = code.make_label().value();
  const auto end = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 10>{
                             simple(Opcode::IConst3), simple(Opcode::IConst5), Instruction::branch(Opcode::Goto, head).value(),
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, end).value(), Instruction::increment(0, -1),
                             simple(Opcode::Swap), Instruction::branch(Opcode::Goto, head).value(), simple(Opcode::ISub),
                             simple(Opcode::IReturn)})
                 .value();
  code.bind_label(head, {ids[3]}).value();
  code.bind_label(end, {ids[8]}).value();
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* branch = find_operation(*graph, ids[4]);
  const auto* back = find_operation(*graph, ids[7]);
  ASSERT_NE(branch, nullptr);
  ASSERT_NE(back, nullptr);
  const auto* header = graph->block(branch->block);
  ASSERT_NE(header, nullptr);
  std::vector<std::pair<SsaValueId, SsaValueId>> back_values;
  for (const auto phi : header->phis) {
    for (const auto [edge, value] : graph->value(phi)->incoming)
      if (graph->edge(edge)->source == back->block)
        back_values.emplace_back(phi, value);
  }
  EXPECT_TRUE(std::ranges::any_of(back_values, [&](const auto& pair) {
    return pair.first != pair.second && std::ranges::find(back_values, std::pair{pair.second, pair.first}) != back_values.end();
  }));
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, ExceptionalPhiInputsSeeLocalsAtEachProtectedInstruction) {
  MethodFixture fixture("(II)I");
  auto& code = fixture.code();
  const auto from = code.make_label().value();
  const auto to = code.make_label().value();
  const auto handler = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 17>{
                             Instruction::bipush(20), Instruction::istore(2), simple(Opcode::IConst1), Instruction::iload(0),
                             simple(Opcode::IDiv), Instruction::pop(), Instruction::bipush(21), Instruction::istore(2),
                             simple(Opcode::IConst1), Instruction::iload(1), simple(Opcode::IDiv), Instruction::pop(),
                             Instruction::iload(2), simple(Opcode::IReturn), Instruction::pop(), Instruction::iload(2),
                             simple(Opcode::IReturn)})
                 .value();
  code.bind_label(from, {ids[2]}).value();
  code.bind_label(to, {ids[12]}).value();
  code.bind_label(handler, {ids[14]}).value();
  fixture.owner.edit_code(fixture.index)->set_exception_handlers(std::array{ExceptionHandler{from, to, handler, {}}});

  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  ASSERT_EQ(graph->handlers().size(), 1U);
  const auto* initial = find_operation(*graph, ids[0]);
  const auto* changed = find_operation(*graph, ids[6]);
  const auto* ret = find_operation(*graph, ids[16]);
  ASSERT_NE(initial, nullptr);
  ASSERT_NE(changed, nullptr);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->inputs.size(), 1U);
  const auto* phi = graph->value(ret->inputs.front());
  ASSERT_NE(phi, nullptr);
  ASSERT_EQ(phi->kind, SsaValueKind::Phi);
  bool saw_early = false;
  bool saw_late = false;
  for (const auto [edge_id, value] : phi->incoming) {
    const auto* edge = graph->edge(edge_id);
    ASSERT_NE(edge, nullptr);
    ASSERT_EQ(edge->kind, SsaEdgeKind::Exceptional);
    EXPECT_EQ(edge->handler, 0U);
    const auto* site = graph->operation(edge->site);
    ASSERT_NE(site, nullptr);
    ASSERT_NE(graph->value(edge->exception), nullptr);
    EXPECT_EQ(graph->value(edge->exception)->kind, SsaValueKind::Exception);
    if (site->origin == ids[4]) {
      saw_early = true;
      EXPECT_EQ(value, initial->result);
    } else if (site->origin == ids[10]) {
      saw_late = true;
      EXPECT_EQ(value, changed->result);
    }
  }
  EXPECT_TRUE(saw_early);
  EXPECT_TRUE(saw_late);
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, MergesNormalFlowIntoAHandlerWithTheJvmExceptionStackValue) {
  MethodFixture fixture("(Ljava/lang/Throwable;I)Ljava/lang/Throwable;");
  auto& code = fixture.code();
  const auto protected_start = code.make_label().value();
  const auto shared = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 7>{
                             Instruction::iload(1), Instruction::branch(Opcode::IfEq, protected_start).value(),
                             Instruction::aload(0), Instruction::branch(Opcode::Goto, shared).value(),
                             Instruction::aload(0), simple(Opcode::AThrow), simple(Opcode::AReturn)})
                 .value();
  code.bind_label(protected_start, {ids[4]}).value();
  code.bind_label(shared, {ids[6]}).value();
  fixture.owner.edit_code(fixture.index)->set_exception_handlers(std::array{ExceptionHandler{protected_start, shared, shared, {}}});
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* ret = find_operation(*graph, ids[6]);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->inputs.size(), 1U);
  const auto* exception_or_parameter = graph->value(ret->inputs.front());
  ASSERT_NE(exception_or_parameter, nullptr);
  ASSERT_EQ(exception_or_parameter->kind, SsaValueKind::Phi);
  bool saw_throw = false;
  bool saw_jump = false;
  for (const auto [id, input] : exception_or_parameter->incoming) {
    const auto* edge = graph->edge(id);
    ASSERT_NE(edge, nullptr);
    if (edge->kind == SsaEdgeKind::Exceptional) {
      EXPECT_EQ(input, edge->exception);
      const auto* site = graph->operation(edge->site);
      ASSERT_NE(site, nullptr);
      saw_throw |= site->origin == ids[5];
    } else {
      EXPECT_EQ(edge->kind, SsaEdgeKind::Unconditional);
      EXPECT_EQ(input, graph->parameters().front());
      saw_jump = true;
    }
  }
  EXPECT_TRUE(saw_throw);
  EXPECT_TRUE(saw_jump);
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, PreservesDuplicateSwitchArmsSharingABlock) {
  MethodFixture fixture("(I)I");
  auto& code = fixture.code();
  const auto first = code.make_label().value();
  const auto other = code.make_label().value();
  (void)code.append(Instruction::iload(0)).value();
  const std::array targets{first, other, first};
  const auto select = code.append(TableSwitch{-2, other, targets}).value();
  auto ids = code.append(std::array<InstructionBuilder, 4>{
                             simple(Opcode::IConst1), simple(Opcode::IReturn), simple(Opcode::IConst2), simple(Opcode::IReturn)})
                 .value();
  code.bind_label(first, {ids[0]}).value();
  code.bind_label(other, {ids[2]}).value();
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* op = find_operation(*graph, select);
  ASSERT_NE(op, nullptr);
  const auto* block = graph->block(op->block);
  ASSERT_NE(block, nullptr);
  std::vector<std::pair<std::int32_t, SsaBlockId>> cases;
  std::optional<SsaBlockId> fallback;
  for (const auto id : block->outgoing) {
    const auto* edge = graph->edge(id);
    ASSERT_NE(edge, nullptr);
    if (edge->kind == SsaEdgeKind::SwitchDefault) {
      ASSERT_FALSE(fallback);
      fallback = edge->target;
    } else {
      ASSERT_EQ(edge->kind, SsaEdgeKind::SwitchCase);
      ASSERT_TRUE(edge->case_key);
      cases.emplace_back(*edge->case_key, edge->target);
    }
  }
  std::ranges::sort(cases, {}, &decltype(cases)::value_type::first);
  ASSERT_EQ(cases.size(), 3U);
  EXPECT_EQ(cases[0].first, -2);
  EXPECT_EQ(cases[1].first, -1);
  EXPECT_EQ(cases[2].first, 0);
  EXPECT_EQ(cases[0].second, cases[2].second);
  EXPECT_NE(cases[0].second, cases[1].second);
  EXPECT_EQ(fallback, cases[1].second);
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, RejectsUnseededDeadCodeAndPreservesDeclaredDeadStackInputs) {
  MethodFixture fixture("()I");
  auto& code = fixture.code();
  (void)code.append(simple(Opcode::IConst1)).value();
  (void)code.append(simple(Opcode::IReturn)).value();
  const auto dead = code.append(simple(Opcode::IReturn)).value();
  auto rejected = fixture.lift();
  ASSERT_FALSE(rejected);
  ASSERT_TRUE(rejected.error().analysis);
  EXPECT_EQ(rejected.error().analysis->kind, AnalysisErrorKind::UnreachableCode);
  EXPECT_EQ(rejected.error().analysis->instruction, dead);

  // same_locals_1_stack_item_frame at offset 2 with one INTEGER stack value.
  std::vector<Attribute> attributes;
  attributes.push_back(Attribute::raw(fixture.owner.constant_pool().append_utf8("StackMapTable").value(),
                                      {std::byte{0}, std::byte{1}, std::byte{66}, std::byte{1}}));
  fixture.owner.edit_code(fixture.index)->supply_metadata(1, 0, {}, std::move(attributes));
  const auto seeded_bytes = write_class(fixture.owner, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata});
  ASSERT_TRUE(seeded_bytes) << seeded_bytes.error().detail;
  auto seeded = read_class(seeded_bytes->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(seeded) << seeded.error().detail;
  auto graph = lift_ssa(seeded->class_file, fixture.index, fixture.context);
  ASSERT_TRUE(graph) << graph.error().detail;
  bool saw_dead_root = false;
  for (const auto& block : graph->blocks())
    for (const auto id : block.incoming)
      saw_dead_root |= graph->edge(id)->kind == SsaEdgeKind::DeadRoot;
  EXPECT_TRUE(saw_dead_root);
  auto result = lower_ssa(seeded->class_file, *graph, fixture.context);
  ASSERT_TRUE(result) << result.error().detail;
  auto assembled = write_class(seeded->class_file);
  ASSERT_TRUE(assembled) << assembled.error().detail;
  auto parsed = read_class(assembled->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto lifted_again = lift_ssa(parsed->class_file, fixture.index, fixture.context);
  ASSERT_TRUE(lifted_again) << lifted_again.error().detail;
}

TEST(SsaEditing, AuthorsTypedWideParametersAndTracksRepeatedUsesThroughReplacement) {
  MethodFixture fixture("(JJ)J", false);
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto parameters = graph->parameters();
  ASSERT_EQ(parameters.size(), 2U);
  const auto first = parameters[0];
  const auto second = parameters[1];
  EXPECT_EQ(graph->value(first)->slot, 0U);
  EXPECT_EQ(graph->value(second)->slot, 2U);
  EXPECT_EQ(graph->value(second)->type, LONG);
  const auto block = graph->entry_block();
  const auto sum = graph->append_operation(block, simple(Opcode::LAdd), std::array{first, first}, LONG).value();
  const auto total = graph->operation(sum)->result;
  const auto ret = graph->append_terminator(block, Opcode::LReturn, std::array{total}).value();
  auto twice = graph->uses(first);
  ASSERT_TRUE(twice);
  EXPECT_EQ(twice->size(), 2U);
  EXPECT_FALSE(graph->erase_operation(sum));
  EXPECT_EQ(graph->operation(ret)->inputs.front(), total);
  ASSERT_TRUE(graph->replace_uses(first, second));
  auto no_use = graph->uses(first);
  ASSERT_TRUE(no_use);
  EXPECT_TRUE(no_use->empty());
  EXPECT_TRUE(std::ranges::equal(graph->operation(sum)->inputs, std::array{second, second}));
  expect_writable(fixture, *graph);
}

TEST(SsaEditing, RejectsForeignAndRetiredGraphHandlesWithoutChangingOperations) {
  MethodFixture fixture("(I)I", false);
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context);
  auto foreign = create_ssa(fixture.owner, fixture.index, fixture.context);
  ASSERT_TRUE(graph);
  ASSERT_TRUE(foreign);
  const auto input = graph->parameters().front();
  const auto root = graph->entry_block();
  const auto temp = graph->append_operation(root, simple(Opcode::IConst2), {}, INT).value();
  const auto retired = graph->operation(temp)->result;
  ASSERT_TRUE(graph->erase_operation(temp));
  EXPECT_EQ(graph->operation(temp), nullptr);
  EXPECT_EQ(graph->value(retired), nullptr);
  const auto ret = graph->append_terminator(root, Opcode::IReturn, std::array{input}).value();
  for (const auto bad : {foreign->parameters().front(), retired, SsaValueId{}}) {
    auto update = graph->set_input(ret, 0, bad);
    ASSERT_FALSE(update);
    EXPECT_EQ(update.error().kind, SsaErrorKind::InvalidHandle);
    EXPECT_EQ(graph->operation(ret)->inputs.front(), input);
  }
  auto uses = graph->uses(foreign->parameters().front());
  EXPECT_FALSE(uses);
  const auto insert = graph->append_operation(foreign->entry_block(), simple(Opcode::Nop));
  ASSERT_FALSE(insert);
  EXPECT_EQ(insert.error().kind, SsaErrorKind::InvalidHandle);
  expect_writable(fixture, *graph);
}

TEST(SsaEditing, RejectsAnIncompletePhiAndANondominatingDefinitionBeforeCommit) {
  MethodFixture fixture("(I)I", false);
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto start = graph->entry_block();
  const auto alternate = graph->add_block().value();
  const auto join = graph->add_block().value();
  const auto parameter = graph->parameters().front();
  (void)graph->append_terminator(start, Opcode::IfEq, std::array{parameter}).value();
  const auto direct = graph->add_edge(start, join, SsaEdgeKind::Conditional).value();
  (void)graph->add_edge(start, alternate, SsaEdgeKind::Fallthrough).value();
  const auto temp = graph->append_operation(alternate, simple(Opcode::IConst2), {}, INT).value();
  const auto local = graph->operation(temp)->result;
  (void)graph->append_terminator(alternate, Opcode::Goto).value();
  const auto indirect = graph->add_edge(alternate, join, SsaEdgeKind::Unconditional).value();
  const auto phi = graph->add_phi(join, INT).value();
  ASSERT_TRUE(graph->set_phi_input(phi, direct, parameter));
  const auto ret = graph->append_terminator(join, Opcode::IReturn, std::array{phi}).value();

  const auto pool_size = fixture.owner.constant_pool().size();
  EXPECT_FALSE(verify_ssa(fixture.owner, *graph, fixture.context));
  EXPECT_FALSE(lower_ssa(fixture.owner, *graph, fixture.context));
  EXPECT_EQ(fixture.owner.edit_code(fixture.index), nullptr);
  EXPECT_EQ(fixture.owner.constant_pool().size(), pool_size);

  ASSERT_TRUE(graph->set_phi_input(phi, indirect, local));
  ASSERT_TRUE(verify_ssa(fixture.owner, *graph, fixture.context));
  ASSERT_TRUE(graph->set_input(ret, 0, local));
  EXPECT_FALSE(verify_ssa(fixture.owner, *graph, fixture.context));
  EXPECT_FALSE(lower_ssa(fixture.owner, *graph, fixture.context));
  EXPECT_EQ(fixture.owner.edit_code(fixture.index), nullptr);
  EXPECT_EQ(fixture.owner.constant_pool().size(), pool_size);
  ASSERT_TRUE(graph->set_input(ret, 0, phi));
  expect_writable(fixture, *graph);
}

TEST(SsaConstruction, InitializationReplacesEveryAliasOfOneAllocation) {
  MethodFixture fixture("()LSsaFixture;");
  auto& pool = fixture.owner.constant_pool();
  const auto name = pool.append_utf8("<init>").value();
  const auto descriptor = pool.append_utf8("()V").value();
  const auto pair = pool.append(CpEntry{CpTag::NameAndType, 0, name, descriptor}).value();
  const auto constructor = pool.append(CpEntry{CpTag::Methodref, 0, fixture.owner.this_class(), pair}).value();
  auto& code = fixture.code();
  const auto same = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 11>{
                             Instruction::constant(Opcode::New, fixture.owner.this_class()).value(), simple(Opcode::Dup),
                             simple(Opcode::Dup), Instruction::astore(0), Instruction::constant(Opcode::InvokeSpecial, constructor).value(),
                             Instruction::aload(0), Instruction::branch(Opcode::IfACmpEq, same).value(),
                             simple(Opcode::AConstNull), simple(Opcode::AReturn), Instruction::aload(0), simple(Opcode::AReturn)})
                 .value();
  code.bind_label(same, {ids[9]}).value();
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* allocation = find_operation(*graph, ids[0]);
  const auto* init = find_operation(*graph, ids[4]);
  const auto* cmp = find_operation(*graph, ids[6]);
  ASSERT_NE(allocation, nullptr);
  ASSERT_NE(init, nullptr);
  ASSERT_NE(cmp, nullptr);
  ASSERT_NE(graph->value(allocation->result), nullptr);
  const auto initial = graph->value(allocation->result)->type;
  EXPECT_EQ(initial.kind, VerificationKind::Uninitialized);
  EXPECT_EQ(initial.allocation, allocation->id);
  ASSERT_EQ(init->inputs.size(), 1U);
  EXPECT_EQ(init->inputs.front(), allocation->result);
  ASSERT_NE(graph->value(init->result), nullptr);
  EXPECT_EQ(graph->value(init->result)->type.kind, VerificationKind::Object);
  EXPECT_EQ(graph->value(init->result)->type.reference, "SsaFixture");
  EXPECT_TRUE(std::ranges::equal(cmp->inputs, std::array{init->result, init->result}));
  expect_writable(fixture, *graph);
}

TEST(SsaLowering, DetectsStaleInstructionAndMetadataSnapshotsAndPreservesNewerBytes) {
  for (const bool edit_code : {false, true}) {
    SCOPED_TRACE(edit_code);
    MethodFixture fixture("()I");
    auto& code = fixture.code();
    const auto source = code.append(simple(Opcode::IConst1)).value();
    (void)code.append(simple(Opcode::IReturn)).value();
    auto* body = fixture.owner.edit_code(fixture.index);
    body->supply_metadata(1, 0, {}, {});
    auto graph = fixture.lift();
    ASSERT_TRUE(graph) << graph.error().detail;
    EXPECT_TRUE(graph->is_current(fixture.owner, fixture.context));
    if (edit_code) {
      code.replace(source, simple(Opcode::IConst2)).value();
    } else {
      // The bytecode revision stays current while the attributes change.
      std::vector<Attribute> nested;
      nested.push_back(Attribute::raw(fixture.owner.constant_pool().append_utf8("LineNumberTable").value(),
                                      {std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{9}}));
      body->supply_metadata(1, 0, {}, std::move(nested));
    }
    EXPECT_FALSE(graph->is_current(fixture.owner, fixture.context));
    const auto revision = code.revision();
    const auto size = fixture.owner.constant_pool().size();
    const WriteOptions options{.code_policy = CodeWritePolicy::CallerSuppliedMetadata,
                               .code_analysis = edit_code ? CodeAnalysisMode::ComputeFrames : CodeAnalysisMode::Preserve};
    const auto before = write_class(fixture.owner, options);
    ASSERT_TRUE(before) << before.error().detail;
    const auto lower = lower_ssa(fixture.owner, *graph, fixture.context);
    ASSERT_FALSE(lower);
    EXPECT_EQ(lower.error().kind, SsaErrorKind::StaleSource);
    EXPECT_EQ(fixture.owner.edit_code(fixture.index), body);
    EXPECT_EQ(fixture.code().revision(), revision);
    EXPECT_EQ(fixture.owner.constant_pool().size(), size);
    const auto after = write_class(fixture.owner, options);
    ASSERT_TRUE(after) << after.error().detail;
    EXPECT_EQ(after->bytes, before->bytes);
  }
}

TEST(SsaLowering, ReportsOpaqueCodeAttributeBeforeCommitAndDropsItOnlyByRequest) {
  MethodFixture fixture("()I");
  auto& code = fixture.code();
  const auto original = code.append(simple(Opcode::IConst1)).value();
  const auto returned = code.append(simple(Opcode::IReturn)).value();
  std::vector<Attribute> nested;
  nested.push_back(Attribute::raw(fixture.owner.constant_pool().append_utf8("PrivateCodeOffsets").value(), {std::byte{1}}));
  fixture.owner.edit_code(fixture.index)->supply_metadata(1, 0, {}, std::move(nested));
  auto graph = fixture.lift();
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto* ret = find_operation(*graph, returned);
  ASSERT_NE(ret, nullptr);
  const auto return_id = ret->id;
  const auto replacement = graph->insert_before(return_id, simple(Opcode::IConst2), {}, INT).value();
  ASSERT_TRUE(graph->set_input(return_id, 0, graph->operation(replacement)->result));
  const auto original_size = fixture.owner.constant_pool().size();
  const auto original_revision = code.revision();
  const auto strict = lower_ssa(fixture.owner, *graph, fixture.context);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, SsaErrorKind::UnsafeMetadata);
  EXPECT_EQ(fixture.owner.constant_pool().size(), original_size);
  EXPECT_EQ(fixture.code().get(original).value().opcode(), Opcode::IConst1);
  EXPECT_EQ(fixture.code().revision(), original_revision);
  auto preserved = fixture.owner.edit_code(fixture.index)->attributes();
  ASSERT_EQ(preserved.size(), 1U);
  const auto* raw = preserved.front().get<RawAttribute>();
  ASSERT_NE(raw, nullptr);
  const auto raw_bytes = raw->bytes(fixture.owner);
  ASSERT_TRUE(raw_bytes);
  EXPECT_TRUE(std::ranges::equal(*raw_bytes, std::array{std::byte{1}}));
  auto dropped = lower_ssa(fixture.owner, *graph, fixture.context, {.drop_unsafe_code_attributes = true});
  ASSERT_TRUE(dropped) << dropped.error().detail;
  ASSERT_TRUE(std::ranges::any_of(dropped->diagnostics, [](const Diagnostic& diagnostic) {
    return diagnostic.code == DiagnosticCode::DroppedUnsafeCodeAttribute;
  }));
  const auto output = write_class(fixture.owner);
  ASSERT_TRUE(output) << output.error().detail;
}

TEST(SsaConstruction, ReportsLegacySubroutineAndCancellationWithoutMutatingCode) {
  MethodFixture fixture("()V");
  auto& code = fixture.code();
  const auto sub = code.make_label().value();
  const auto call = code.append(Instruction::branch(Opcode::Jsr, sub).value()).value();
  (void)code.append(Instruction::return_void()).value();
  const auto target = code.append(Instruction::astore(0)).value();
  (void)code.append(Instruction::local(Opcode::Ret, 0).value()).value();
  code.bind_label(sub, {target}).value();
  const auto revision = code.revision();
  auto invalid = fixture.lift();
  ASSERT_FALSE(invalid);
  ASSERT_TRUE(invalid.error().analysis);
  EXPECT_EQ(invalid.error().analysis->kind, AnalysisErrorKind::UnsupportedSubroutineAnalysis);
  EXPECT_EQ(code.get(call).value().opcode(), Opcode::Jsr);
  EXPECT_EQ(code.revision(), revision);

  MethodFixture valid("()V");
  (void)valid.code().append(Instruction::return_void()).value();
  std::stop_source source;
  source.request_stop();
  AnalysisContext cancelled(AnalysisLimits{.cancellation = source.get_token()});
  auto result = lift_ssa(valid.owner, valid.index, cancelled);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, SsaErrorKind::Cancelled);
}

TEST(SsaEditing, AnExceptionalEdgeCannotUseTheThrowingOperationsSuccessValue) {
  MethodFixture fixture("(I)I", false);
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context);
  ASSERT_TRUE(graph) << graph.error().detail;
  const auto entry = graph->entry_block();
  const auto handler = graph->add_block().value();
  const auto parameter = graph->parameters().front();
  const auto divide = graph->append_operation(entry, simple(Opcode::IDiv), std::array{parameter, parameter}, INT).value();
  const auto quotient = graph->operation(divide)->result;
  (void)graph->append_terminator(entry, Opcode::IReturn, std::array{quotient}).value();
  const auto row = graph->add_handler(handler, {}).value();
  const auto exceptional = graph->add_exception_edge(divide, row, {VerificationKind::Object, "java/lang/Throwable"}).value();
  const auto saved = graph->add_phi(handler, INT).value();
  ASSERT_TRUE(graph->set_phi_input(saved, exceptional, quotient));
  (void)graph->append_terminator(handler, Opcode::IReturn, std::array{saved}).value();
  const auto invalid = verify_ssa(fixture.owner, *graph, fixture.context);
  ASSERT_FALSE(invalid);
  EXPECT_FALSE(lower_ssa(fixture.owner, *graph, fixture.context));
  EXPECT_EQ(fixture.owner.edit_code(fixture.index), nullptr);
  ASSERT_TRUE(graph->set_phi_input(saved, exceptional, parameter));
  expect_writable(fixture, *graph);
}

TEST(SsaEditing, FailedConstructorInvalidatesOnlyItsOwnAllocationInAHandler) {
  MethodFixture fixture("()V", false);
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context).value();
  const auto entry = graph.entry_block();
  const auto handler = graph.add_block().value();
  const auto allocate = Instruction::constant(Opcode::New, fixture.owner.this_class()).value();
  const SsaType raw{VerificationKind::Uninitialized, "SsaFixture"};
  const auto first = graph.append_operation(entry, allocate, {}, raw).value();
  const auto first_value = graph.operation(first)->result;
  const auto second = graph.append_operation(entry, allocate, {}, raw).value();
  const auto second_value = graph.operation(second)->result;
  const MethodInvocation ctor{Opcode::InvokeSpecial, "SsaFixture", "<init>", "()V"};
  const auto attempt = graph.append_symbolic(entry, ctor, std::array{first_value}).value();
  graph.append_terminator(entry, Opcode::Return).value();
  const auto row = graph.add_handler(handler, {}).value();
  graph.add_exception_edge(attempt, row, {VerificationKind::Object, "java/lang/Throwable"}).value();
  const auto recovery = graph.append_symbolic(handler, ctor, std::array{second_value}).value();
  graph.append_terminator(handler, Opcode::Return).value();
  auto valid = verify_ssa(fixture.owner, graph, fixture.context);
  ASSERT_TRUE(valid) << valid.error().detail;
  ASSERT_TRUE(graph.set_input(recovery, 0, first_value));
  auto invalid = verify_ssa(fixture.owner, graph, fixture.context);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().kind, SsaErrorKind::TypeMismatch);
  EXPECT_EQ(invalid.error().operation, recovery);
  ASSERT_TRUE(graph.set_input(recovery, 0, second_value));
  expect_writable(fixture, graph);
}

TEST(SsaEditing, AuthoredStaticnessMismatchRemainsALinkageTimeError) {
  MethodFixture fixture("()I", false);
  auto& pool = fixture.owner.constant_pool();
  fixture.owner.add_field(0x0009, pool.append_utf8("value").value(), pool.append_utf8("I").value()).value();
  auto graph = create_ssa(fixture.owner, fixture.index, fixture.context).value();
  const auto entry = graph.entry_block();
  const auto null = graph.append_operation(entry, simple(Opcode::AConstNull), {}, SsaType{VerificationKind::Null}).value();
  const auto receiver = graph.operation(null)->result;
  const auto read = graph.append_symbolic(entry, FieldAccess{Opcode::GetField, "SsaFixture", "value", "I"}, std::array{receiver}, INT).value();
  graph.append_terminator(entry, Opcode::IReturn, std::array{graph.operation(read)->result}).value();
  expect_writable(fixture, graph);
  // Standalone frame computation retains its original stricter member consistency checks.
  const auto strict = compute_frames(fixture.owner, fixture.owner.methods()[fixture.index], fixture.context);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, AnalysisErrorKind::InvalidCode);
}

TEST(SsaLowering, PreservesDistinctConstantDynamicResolutionEntriesWithIdenticalPayloads) {
  const auto path = std::filesystem::path(HIPPOCAFE_FIXTURE_DIR) / "generated" / "DynamicConstant.class";
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  auto parsed = read_class(std::as_bytes(std::span(bytes)), {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto& owner = parsed->class_file;
  auto& code = owner.edit_code(0)->code();
  const auto original = code.at(0);
  const auto index = code.get(original).value().constant_index().value();
  const auto* entry = owner.constant_pool().get(index);
  ASSERT_NE(entry, nullptr);
  const auto constant = *entry;
  ASSERT_EQ(constant.tag, CpTag::Dynamic);
  const auto duplicate = owner.constant_pool().append(constant).value();
  ASSERT_NE(index, duplicate);
  (void)code.insert_before(original, Instruction::constant(Opcode::Ldc, duplicate).value()).value();
  (void)code.insert_before(original, Instruction::pop()).value();
  AnalysisContext context;
  auto graph = lift_ssa(owner, 0, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  auto output = write_class(owner);
  ASSERT_TRUE(output) << output.error().detail;
  auto reparsed = read_class(output->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  const auto& final_code = reparsed->class_file.methods()[0].code()->code();
  std::vector<CpIndex> constants;
  for (std::size_t i = 0; i < final_code.size(); ++i) {
    const auto instruction = final_code.get(final_code.at(i)).value();
    if (const auto operand = instruction.constant_index()) {
      const auto* cp = reparsed->class_file.constant_pool().get(*operand);
      ASSERT_NE(cp, nullptr);
      if (cp->tag == CpTag::Dynamic)
        constants.push_back(*operand);
    }
  }
  EXPECT_TRUE(std::ranges::equal(constants, std::array{duplicate, index}));
}

} // namespace
} // namespace cafe::analysis
