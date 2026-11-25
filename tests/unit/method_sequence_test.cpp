#include <array>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <hippo/cafe.hpp>

namespace cafe {
namespace {

namespace i = cafe::insn;
namespace p = cafe::pattern;

constexpr WriteOptions SUPPLIED{.code_policy = CodeWritePolicy::CallerSuppliedMetadata};

ClassFile sequence_class() {
  auto file = ClassFile::create({55, 0}, "Sequences").value();
  auto& pool = file.constant_pool();
  const auto descriptor = pool.append_utf8("()V").value();
  const auto code_name = pool.append_utf8("Code").value();
  for (const auto name : {"run", "other"}) {
    const auto method = file.add_method(0x0009, pool.append_utf8(name).value(), descriptor).value();
    auto& body = file.add_code(method, code_name).value().get();
    body.code().append(Instruction::return_void()).value();
    body.supply_metadata(0, 0, {});
  }
  return file;
}

TEST(MethodSequence, InsertsOnEitherSideOfLabelsWithoutChangingLegacyInstructionPlacement) {
  auto file = sequence_class();
  auto editor = file.edit_method("run", "()V").value();
  const auto ret = (*editor.instructions().begin()).id();
  const auto match = editor.find_first(p::opcode(Opcode::Return)).value().value();
  const auto first = editor.label_before(ret).value();
  const auto second = editor.label_before(ret).value();
  const auto third = editor.label_before(ret).value();
  const auto end = editor.label_at_end().value();
  const auto& body = editor.code()->code();
  const auto jump = editor.insert_before(first, i::goto_(second)).value();
  const auto branch_match = editor.find_first(p::opcode(Opcode::Goto)).value().value();

  const auto first_patch = editor.insert_before(second, {i::nop()}).value().front();
  EXPECT_EQ(body.label_position(first)->before, first_patch);
  EXPECT_EQ(body.label_position(second)->before, ret);
  const auto build_patch = [](CodeBuilder& code) {
    const auto local = code.label();
    const auto entry = code.label();
    const auto trailing = code.label();
    code.bind(entry).bind(local).goto_(local).bind(trailing);
  };
  const auto second_patch = editor.insert_after(second, build_patch).value().front();

  const auto nodes = editor.nodes();
  ASSERT_EQ(nodes.size(), 11u);
  EXPECT_EQ(nodes[0], CodeNode{jump});
  EXPECT_EQ(nodes[1], CodeNode{first});
  EXPECT_EQ(nodes[2], CodeNode{first_patch});
  EXPECT_EQ(nodes[3], CodeNode{second});
  EXPECT_EQ(nodes[5], CodeNode{body.branch_target(second_patch).value()});
  EXPECT_EQ(nodes[6], CodeNode{second_patch});
  EXPECT_EQ(nodes[8], CodeNode{third});
  EXPECT_EQ(nodes[9], CodeNode{ret});
  EXPECT_EQ(nodes[10], CodeNode{end});
  EXPECT_TRUE(editor.instruction(ret)->is(Opcode::Return));
  EXPECT_EQ(body.branch_target(jump).value(), second);
  EXPECT_EQ(body.label_position(second)->before, second_patch);
  EXPECT_EQ(editor.erase(branch_match).error().cause, CodeEditError::StaleMatch);

  const auto ordered = editor.insert_before(nodes[9], i::nop()).value();
  EXPECT_EQ(body.label_position(third)->before, ordered);
  const auto late = editor.label_before(ret).value();
  EXPECT_TRUE(editor.insert_before(ret, i::nop()));
  EXPECT_EQ(body.label_position(late)->before, ret);
  EXPECT_EQ(editor.replace(match, {i::nop(), Instruction::return_void()})->front(), ret);

  const auto new_end = editor.label_at_end().value();
  const auto appended = editor.insert_after(end, i::goto_(end)).value();
  EXPECT_EQ(body.label_position(end)->before, appended);
  EXPECT_EQ(body.label_position(new_end)->before, std::nullopt);
  const auto mark_only = editor.insert_before(new_end, [](CodeBuilder& code) { code.bind(code.label()); });
  ASSERT_TRUE(mark_only);
  EXPECT_TRUE(mark_only->empty());
  const auto before = editor.nodes();
  const auto trailing = std::get<LabelId>(before[before.size() - 2]);
  EXPECT_NE(trailing, new_end);
  EXPECT_EQ(body.label_position(trailing)->before, std::nullopt);
  const auto unbound = editor.code()->code().make_label().value();
  EXPECT_EQ(editor.insert_before(unbound, i::nop()), std::unexpected(CodeEditError::UnboundLabel));
  EXPECT_EQ(editor.insert_after(LabelId{}, i::nop()), std::unexpected(CodeEditError::InvalidLabel));
  EXPECT_EQ(editor.nodes(), before);
}

TEST(MethodSequence, InsertsListsRangesAndBuildersInCallerOrder) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  const auto& body = editor.code()->code();
  const auto anchor = body.at(0);
  const auto revision = body.revision();
  const auto& other = file.methods()[1].code()->code();
  const auto other_revision = other.revision();
  const auto other_entry = other.at(0);
  const auto first = editor.insert_before(anchor, {i::bipush(1), i::pop()}).value();
  const std::vector<MethodInstruction> second_patch{i::bipush(2), i::pop()};
  const auto second = editor.insert_after(first.back(), second_patch).value();
  const std::array third_patch{i::bipush(3), i::pop()};
  const auto third = editor.insert_after(second.back(), std::span<const MethodInstruction>(third_patch)).value();
  CodeBuilder fourth_patch;
  fourth_patch.bipush(4).pop();
  const auto fourth = editor.insert_before(anchor, fourth_patch).value();
  std::istringstream numbers("5 0");
  auto input = std::ranges::istream_view<int>(numbers) | std::views::transform([](int number) {
                 return number == 0 ? i::pop() : i::bipush(number);
               });
  const auto fifth = editor.insert_after(fourth.back(), input).value();
  auto empty = editor.insert_before(anchor, {});
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
  EXPECT_TRUE(editor.replace(anchor, Instruction::return_void()));
  EXPECT_EQ(body.revision(), revision + 5);

  std::size_t position = 0;
  int value = 1;
  for (const auto* ids : {&first, &second, &third, &fourth, &fifth}) {
    ASSERT_EQ(ids->size(), 2u);
    EXPECT_EQ(body.at(position++), ids->front());
    EXPECT_EQ(body.get(ids->front())->immediate_value(), value++);
    EXPECT_EQ(body.at(position++), ids->back());
    EXPECT_EQ(body.get(ids->back())->opcode(), Opcode::Pop);
  }
  EXPECT_EQ(body.at(position), anchor);
  EXPECT_EQ(other.revision(), other_revision);
  EXPECT_EQ(other.at(0), other_entry);
  EXPECT_EQ(other.size(), 1u);
  EXPECT_TRUE(write_class(file, {.code_analysis = CodeAnalysisMode::ComputeFrames}));
}

TEST(MethodSequence, FailedMixedPatchesRestorePoolBootstrapAndMetadata) {
  constexpr MethodHandle bootstrap_handle{MethodHandleKind::InvokeStatic, "Bootstrap", "link",
                                          "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                                          "Ljava/lang/Class;)Ljava/lang/Object;"};
  for (const bool existing_table : {false, true}) {
    SCOPED_TRACE(existing_table);
    auto file = sequence_class();
    auto editor = file.edit_method(0).value();
    const auto& body = editor.code()->code();
    const auto anchor = body.at(0);
    if (existing_table) {
      editor.insert_before(anchor, {DynamicConstant{"first", "I", {bootstrap_handle}}, i::pop()}).value();
      editor.code()->supply_metadata(1, 0, {});
    }
    const auto before = write_class(file, SUPPLIED).value();
    const auto revision = body.revision();
    const auto cp_size = file.constant_pool().size();
    const std::array arguments{BootstrapArgument{std::int32_t{13}}};
    const DynamicConstant pending{"second", "I", {bootstrap_handle, arguments}};
    const auto failure = editor.insert_before(anchor, {i::ldc("temporary"), i::pop(), pending, i::pop(), i::getstatic("Broken", "value", "()V")});
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().cause, CodeEditError::InvalidOperand);
    EXPECT_EQ(failure.error().instruction_index, 4u);
    EXPECT_EQ(body.revision(), revision);
    EXPECT_EQ(file.constant_pool().size(), cp_size);
    EXPECT_EQ(file.attributes().size(), existing_table ? 1u : 0u);
    if (existing_table) {
      EXPECT_EQ(file.attributes().front().get<BootstrapMethods>()->methods.size(), 1u);
    }
    const auto unchanged = write_class(file, SUPPLIED);
    ASSERT_TRUE(unchanged) << unchanged.error().detail;
    EXPECT_EQ(unchanged->bytes, before.bytes);

    EXPECT_THROW((void)editor.insert_before(anchor, [](auto& builder) {
      builder.ldc("callback text").pop();
      throw std::runtime_error("application failure");
    }),
                 std::runtime_error);
    EXPECT_EQ(body.revision(), revision);
    EXPECT_EQ(file.constant_pool().size(), cp_size);
    const auto retry = editor.insert_before(anchor, {pending, i::pop()});
    ASSERT_TRUE(retry);
    EXPECT_EQ(body.revision(), revision + 1);
    EXPECT_EQ(file.attributes().front().get<BootstrapMethods>()->methods.size(), existing_table ? 2u : 1u);
  }
}

TEST(MethodSequence, ReplacesAtLabeledEntryAndRejectsRetiredMatches) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  auto& body = editor.code()->code();
  const auto ret = body.at(0);
  const auto ids = editor.insert_before(ret, {i::bipush(1), i::pop(), i::bipush(2), i::pop()}).value();
  const auto entry = body.make_label().value();
  body.bind_label(entry, CodePosition{ids.front()}).value();
  const auto matches = p::find_all(file, 0, p::seq(p::int_constant(), p::opcode(Opcode::Pop))).value();
  ASSERT_EQ(matches.size(), 2u);
  editor.insert_before(matches[1].start(), {i::bipush(10), i::pop()}).value();
  const auto replacement = editor.replace(matches[0], {i::bipush(3), i::pop()}).value();
  ASSERT_EQ(replacement.size(), 2u);
  EXPECT_EQ(replacement.front(), ids.front());
  EXPECT_EQ(body.label_position(entry)->before, replacement.front());
  EXPECT_FALSE(body.get(ids[1]));
  EXPECT_EQ(editor.erase(matches[0]).error().cause, CodeEditError::StaleMatch);
  EXPECT_EQ(file.edit_method(1)->erase(matches[1]).error().cause, CodeEditError::StaleMatch);
  ASSERT_TRUE(editor.erase(matches[1]));
  EXPECT_EQ(editor.erase(matches[1]).error().cause, CodeEditError::StaleMatch);
  EXPECT_TRUE(body.get(ret));
}

TEST(MethodSequence, ChecksSavedContentsContiguityAndInteriorLabelsBeforeCommit) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  auto& body = editor.code()->code();
  const auto ret = body.at(0);
  const auto ids = editor.insert_before(ret, {i::bipush(1), i::pop()}).value();
  const auto pair = p::seq(p::int_constant(), p::opcode(Opcode::Pop));
  const auto original = p::find_first(file, 0, pair).value().value();
  body.replace(ids.front(), Instruction::bipush(2)).value();
  body.replace(ids.front(), Instruction::bipush(1)).value();
  EXPECT_EQ(editor.erase(original).error().cause, CodeEditError::StaleMatch);

  const auto contiguous = p::find_first(file, 0, pair).value().value();
  const auto split = editor.insert_before(ids.back(), i::nop()).value();
  const auto cp_size = file.constant_pool().size();
  const auto failed = editor.replace(contiguous, {i::ldc("should roll back"), i::pop()});
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().cause, CodeEditError::StaleMatch);
  EXPECT_EQ(file.constant_pool().size(), cp_size);
  editor.erase(split).value();

  const auto inside = body.make_label().value();
  body.bind_label(inside, CodePosition{ids.back()}).value();
  const auto labeled = p::find_first(file, 0, pair).value().value();
  const auto revision = body.revision();
  EXPECT_EQ(editor.replace(labeled, {i::nop()}).error().cause, CodeEditError::LabeledInstruction);
  EXPECT_EQ(editor.erase(labeled).error().cause, CodeEditError::LabeledInstruction);
  EXPECT_EQ(body.revision(), revision);
  EXPECT_EQ(body.label_position(inside)->before, ids.back());
}

TEST(MethodSequence, ReportsStaleMatchesBeforeResolvingOrConsumingReplacementInputs) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  const auto match = p::find_first(file, 0, p::any()).value().value();
  editor.erase(match).value();
  const auto revision = editor.code()->code().revision();
  EXPECT_EQ(editor.replace(match, {i::getstatic("Bad", "state", "()V")}).error().cause, CodeEditError::StaleMatch);
  CodeBuilder invalid;
  invalid.bipush(1000);
  EXPECT_EQ(editor.replace(match, invalid).error().cause, CodeEditError::StaleMatch);
  bool invoked = false;
  EXPECT_EQ(editor.replace(match, [&](auto&) { invoked = true; }).error().cause, CodeEditError::StaleMatch);
  EXPECT_FALSE(invoked);
  auto lazy = std::views::single(0) | std::views::transform([&](int) {
                invoked = true;
                return i::nop();
              });
  EXPECT_EQ(editor.replace(match, lazy).error().cause, CodeEditError::StaleMatch);
  EXPECT_FALSE(invoked);
  EXPECT_EQ(editor.code()->code().revision(), revision);
  auto& pool = file.constant_pool();
  const auto native = file.add_method(0x0109, pool.append_utf8("nativeMethod").value(), pool.append_utf8("()V").value()).value();
  EXPECT_EQ(file.edit_method(native)->erase(match).error().cause, CodeEditError::StaleMatch);
}

TEST(MethodSequence, RechecksMatchAfterUserCodeChangesTheSelection) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  const auto match = p::find_first(file, 0, p::any()).value().value();
  const auto initial = editor.code()->code().revision();
  const auto result = editor.replace(match, [&](auto& code) {
    editor.replace(match.start(), i::nop()).value();
    code.bipush(1000);
  });
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().cause, CodeEditError::StaleMatch);
  EXPECT_EQ(editor.code()->code().revision(), initial + 1);
  EXPECT_EQ(editor.code()->code().get(match.start())->opcode(), Opcode::Nop);
}

TEST(MethodSequence, RebindingMatchedBranchAndSwitchOperandsExpiresEvenRestoredTargets) {
  for (const auto opcode : {Opcode::IfEq, Opcode::TableSwitch, Opcode::LookupSwitch}) {
    for (const bool change_default : {true, false}) {
      SCOPED_TRACE(static_cast<int>(opcode));
      SCOPED_TRACE(change_default);
      auto file = sequence_class();
      auto editor = file.edit_method(0).value();
      auto& body = editor.code()->code();
      const auto ret = body.at(0);
      const auto before_ret = editor.insert_before(ret, i::nop()).value();
      const auto moved = body.make_label().value();
      const auto other = body.make_label().value();
      body.bind_label(moved, {before_ret}).value();
      body.bind_label(other, {ret}).value();
      const auto default_target = change_default ? moved : other;
      const auto case_target = change_default ? other : moved;
      const std::array cases{case_target};
      const std::array pairs{LookupPair{5, case_target}};
      if (opcode == Opcode::TableSwitch)
        editor.insert_before(before_ret, i::tableswitch(5, default_target, cases)).value();
      else if (opcode == Opcode::LookupSwitch)
        editor.insert_before(before_ret, i::lookupswitch(default_target, pairs)).value();
      else
        editor.insert_before(before_ret, i::ifeq(moved)).value();
      const auto pattern = p::opcode(opcode);
      const auto saved = p::find_first(file, 0, pattern).value().value();
      body.bind_label(moved, {ret}).value();
      EXPECT_EQ(editor.erase(saved).error().cause, CodeEditError::StaleMatch);
      body.bind_label(moved, {before_ret}).value();
      EXPECT_EQ(editor.erase(saved).error().cause, CodeEditError::StaleMatch);
      EXPECT_TRUE(editor.erase(p::find_first(file, 0, pattern).value().value()));
    }
  }
}

TEST(MethodSequence, InsertAtMovesAnExistingLabelAndExpiresOnlyMatchesThatReferToIt) {
  for (unsigned operation = 0; operation < 3; ++operation) {
    SCOPED_TRACE(operation);
    auto file = sequence_class();
    auto editor = file.edit_method(0).value();
    auto& body = editor.code()->code();
    const auto ret = body.at(0);
    const auto target = body.make_label().value();
    body.bind_label(target, {ret}).value();
    const auto conditional = editor.insert_before(ret, i::ifeq(target)).value();
    const auto saved = p::find_first(file, 0, p::opcode(Opcode::IfEq)).value().value();
    if (operation == 0)
      body.insert_at(target, Instruction::nop()).value();
    else if (operation == 1) {
      const std::array<InstructionBuilder, 2> block{Instruction::nop(), Instruction::nop()};
      body.insert_at(target, block).value();
    } else
      editor.insert_at(target, i::nop()).value();
    EXPECT_EQ(editor.erase(saved).error().cause, CodeEditError::StaleMatch);
    EXPECT_TRUE(body.get(conditional));
  }
}

TEST(MethodSequence, MatchingSurvivesNoOpBindsFallthroughEditsAndFailedLabelInsertions) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  auto& body = editor.code()->code();
  const auto ret = body.at(0);
  const auto target = body.make_label().value();
  const auto unrelated = body.make_label().value();
  const auto conditional = body.insert_before(ret, Branch{Opcode::IfEq, target}).value();
  const auto pattern = p::opcode(Opcode::IfEq);
  const auto unbound = p::find_first(file, 0, pattern).value().value();
  body.bind_label(target, {ret}).value();
  EXPECT_EQ(editor.erase(unbound).error().cause, CodeEditError::StaleMatch);
  const auto bound = p::find_first(file, 0, pattern).value().value();
  const auto revision = body.revision();
  body.bind_label(target, {ret}).value();
  const std::span<const InstructionBuilder> empty;
  EXPECT_TRUE(body.insert_at(target, empty).value().empty());
  EXPECT_FALSE(body.insert_at(target, Branch{Opcode::Nop, target}));
  EXPECT_FALSE(editor.insert_at(target, i::bipush(300)));
  EXPECT_EQ(body.revision(), revision);
  const auto inserted = editor.insert_after(conditional, i::nop()).value();
  body.bind_label(unrelated, {inserted}).value();
  EXPECT_TRUE(editor.erase(bound));
}

TEST(MethodSequence, ReplaysBuilderSwitchesWithLocalDestinationsAndTemporaryCaseArrays) {
  CodeBuilder builder;
  const auto back = builder.label();
  const auto forward = builder.label();
  const auto exit = builder.label();
  builder.bind(back).iconst_5();
  {
    const std::vector cases{back, forward};
    builder.tableswitch(4, exit, cases);
  }
  builder.bind(forward).iconst_1();
  {
    const std::vector pairs{BuilderLookupPair{1, exit}, BuilderLookupPair{2, forward}};
    builder.lookupswitch(back, pairs);
  }
  builder.bind(exit);
  for (const bool insert_after : {false, true}) {
    SCOPED_TRACE(insert_after);
    auto file = sequence_class();
    auto editor = file.edit_method(0).value();
    auto& body = editor.code()->code();
    const auto ret = body.at(0);
    const auto anchor = editor.insert_before(ret, i::nop()).value();
    const auto ids = insert_after ? editor.insert_after(anchor, builder).value() : editor.insert_before(ret, builder).value();
    ASSERT_EQ(ids.size(), 4u);
    const auto target = [&](LabelId label) { return body.label_position(label).value().before; };
    const auto table = body.table_switch(body.switch_id(ids[1]).value()).value();
    EXPECT_EQ(table.low, 4);
    ASSERT_EQ(table.targets.size(), 2u);
    EXPECT_EQ(target(table.default_target), ret);
    EXPECT_EQ(target(table.targets[0]), ids[0]);
    EXPECT_EQ(target(table.targets[1]), ids[2]);
    const auto lookup = body.lookup_switch(body.switch_id(ids[3]).value()).value();
    EXPECT_EQ(target(lookup.default_target), ids[0]);
    ASSERT_EQ(lookup.pairs.size(), 2u);
    EXPECT_EQ(lookup.pairs[0].key, 1);
    EXPECT_EQ(target(lookup.pairs[0].target), ret);
    EXPECT_EQ(lookup.pairs[1].key, 2);
    EXPECT_EQ(target(lookup.pairs[1].target), ids[2]);
    EXPECT_TRUE(write_class(file, {.code_analysis = CodeAnalysisMode::ComputeFrames}));

    const auto revision = body.revision();
    EXPECT_EQ(editor.insert_after(ret, builder).error().cause, CodeEditError::InvalidLabel);
    EXPECT_EQ(body.revision(), revision);
  }
}

TEST(MethodSequence, RejectsUnboundAndForeignSwitchCasesWithAtomicRollback) {
  auto file = sequence_class();
  auto editor = file.edit_method(0).value();
  const auto& body = editor.code()->code();
  const auto anchor = body.at(0);
  const auto bytes = write_class(file, SUPPLIED).value().bytes;
  const auto revision = body.revision();
  const auto cp_size = file.constant_pool().size();
  for (const bool foreign : {false, true}) {
    CodeBuilder builder;
    CodeBuilder other;
    const auto entry = builder.label();
    const auto invalid = foreign ? other.label() : builder.label();
    builder.bind(entry).ldc("discard this string").pop().iconst_1();
    if (foreign) {
      const std::array cases{entry, invalid};
      builder.tableswitch(0, entry, cases);
    } else {
      const std::array pairs{BuilderLookupPair{1, entry}, BuilderLookupPair{2, invalid}};
      builder.lookupswitch(entry, pairs);
    }
    const auto failed = editor.insert_before(anchor, builder);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().cause, foreign ? CodeEditError::InvalidLabel : CodeEditError::UnboundLabel);
    EXPECT_EQ(body.revision(), revision);
    EXPECT_EQ(file.constant_pool().size(), cp_size);
    EXPECT_EQ(write_class(file, SUPPLIED).value().bytes, bytes);
  }
}

} // namespace
} // namespace cafe
