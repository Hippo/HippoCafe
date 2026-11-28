#include <cafe/bytecode/pattern.hpp>
#include <cafe/class_file.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace cafe::pattern {
namespace {

class InstructionPattern : public ::testing::Test {
protected:
  ClassFile file = ClassFile::create({52, 0}, "PatternFixture").value();
  std::size_t index = file.add_method(0x0009, text("run"), text("()V")).value();
  MethodCode& body = file.add_code(index, text("Code")).value().get().code();

  CpIndex text(std::string_view value) {
    return file.constant_pool().append_utf8(value).value();
  }

  InstructionId add(const MethodInstruction& instruction) {
    return file.edit_method(index)->append(instruction).value();
  }
};

TEST_F(InstructionPattern, ComposesSymbolicMembersAndNormalizedLocalIndices) {
  const auto first = add(Instruction::iload(259));
  add(Instruction::nop());
  add(MethodInvocation{Opcode::InvokeStatic, "java/lang/Math", "abs", "(I)I"});
  const auto last = add(Instruction::istore(0));
  add(FieldAccess{Opcode::GetStatic, "Holder", "state", "I"});
  const auto revision = body.revision();
  const auto slots = file.constant_pool().size();
  auto make_pattern = [] {
    const std::string name = "abs";
    return seq(iload(259), repeat(any(), 0, 2),
               invoke({.opcode = Opcode::InvokeStatic, .owner = "java/lang/Math", .name = name, .descriptor = "(I)I"}),
               istore(0));
  };
  const auto found = find_all(file, index, make_pattern()).value();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].start(), first);
  EXPECT_EQ(found[0].instruction_ids().back(), last);
  EXPECT_EQ(found[0].start_index(), 0u);
  EXPECT_EQ(found[0].size(), 4u);
  EXPECT_EQ(found[0].source_revision(), revision);
  EXPECT_TRUE(find_first(file, index, field({.owner = "Holder", .descriptor = "I"}))->has_value());
  EXPECT_FALSE(find_first(file, index, field({.name = "other"}))->has_value());
  EXPECT_FALSE(find_first(file, index, iload(3))->has_value());
  EXPECT_FALSE(find_first(file, index, istore(2))->has_value());
  EXPECT_EQ(body.revision(), revision);
  EXPECT_EQ(file.constant_pool().size(), slots);
}

TEST_F(InstructionPattern, MatchesAllIntegerLiteralFormsAndSkipsOtherConstants) {
  add(Instruction::simple(Opcode::IConstM1).value());
  add(Instruction::simple(Opcode::IConst5).value());
  add(Instruction::bipush(-1));
  add(Instruction::sipush(-32768));
  ASSERT_TRUE(file.constant_pool().append_utf8("unused before pooled literal"));
  CpEntry value{};
  value.tag = CpTag::Integer;
  value.bits = std::bit_cast<std::uint32_t>(std::int32_t{-1});
  auto slot = file.constant_pool().append(value).value();
  add(Instruction::constant(Opcode::Ldc, slot).value());
  value.tag = CpTag::Float;
  add(Instruction::constant(Opcode::Ldc, file.constant_pool().append(value).value()).value());
  const std::array<std::optional<std::int32_t>, 6> literals{-1, 5, -1, -32768, -1, std::nullopt};
  for (std::size_t pos = 0; pos < literals.size(); ++pos)
    EXPECT_EQ(file.instruction({index, body.at(pos)}).value().int_constant(), literals[pos]);
  const auto pooled = file.instruction({index, body.at(4)}).value();
  ASSERT_TRUE(file.compact_constant_pool());
  EXPECT_FALSE(pooled.int_constant());
  EXPECT_EQ(file.instruction(pooled.location()).value().int_constant(), -1);
  EXPECT_EQ(find_all(file, index, int_constant()).value().size(), 5u);
  EXPECT_EQ(find_all(file, index, int_constant(-1)).value().size(), 3u);
  EXPECT_EQ(find_all(file, index, int_constant(5)).value().size(), 1u);
  EXPECT_EQ(find_first(file, index, int_constant(-32768)).value()->start_index(), 3u);
}

TEST_F(InstructionPattern, MovingMatchesAndCapturesTransfersTheirEditableSelection) {
  const auto id = add(Instruction::nop());
  auto selected = find_first(file, index, capture("item", any())).value().value();
  auto group = selected.capture("item").value();
  const auto moved_group = std::move(group);
  const auto moved_match = std::move(selected);
  const auto revision = body.revision();
  auto editor = file.edit_method(index).value();

  EXPECT_TRUE(group.instructions().empty());
  EXPECT_TRUE(selected.instruction_ids().empty());
  EXPECT_EQ(selected.size(), 0u);
  EXPECT_EQ(selected.start(), InstructionId{});
  EXPECT_FALSE(selected.capture("item"));
  EXPECT_EQ(editor.erase(selected).error().cause, CodeEditError::StaleMatch);
  EXPECT_EQ(body.revision(), revision);
  ASSERT_EQ(moved_group.instructions().size(), 1u);
  EXPECT_EQ(moved_group.instructions().front().id(), id);
  EXPECT_EQ(moved_match.start(), id);
  EXPECT_TRUE(editor.erase(moved_match));
}

TEST_F(InstructionPattern, RollsBackFailedAlternativesAndGreedyCaptures) {
  add(Instruction::iload(1));
  add(Instruction::nop());
  add(Instruction::return_void());
  const auto end = opcode(Opcode::Return);
  const auto pattern = seq(either({seq(capture("failed", iload()), end), iload()}),
                           capture("prefix", repeat(any(), 1, 3)), end);
  auto matches = find_all(file, index, pattern);
  ASSERT_TRUE(matches);
  ASSERT_EQ(matches->size(), 1u);
  const auto& match = matches->front();
  EXPECT_FALSE(match.capture("failed"));
  EXPECT_FALSE(match.capture("missing"));
  auto prefix = match.capture("prefix");
  ASSERT_TRUE(prefix);
  ASSERT_EQ(prefix->instructions().size(), 1u);
  EXPECT_TRUE(prefix->instructions().front().is(Opcode::Nop));
  // Captures retain their snapshot independently of the owning Match.
  matches->clear();
  ASSERT_TRUE(body.replace(body.at(1), Instruction::pop()));
  EXPECT_TRUE(prefix->instructions().front().is(Opcode::Nop));
}

TEST_F(InstructionPattern, CapturesEmptyParticipationAndLastRepeatedOccurrence) {
  add(Instruction::iload(1));
  add(Instruction::iload(2));
  add(Instruction::return_void());
  auto name = std::string("load");
  const auto pattern = seq(repeat(capture(name, iload()), 1, 3), capture("empty", optional(istore())),
                           optional(capture("absent", istore())), opcode(Opcode::Return));
  name.clear();
  auto match = find_first(file, index, pattern).value();
  ASSERT_TRUE(match);
  const auto load = match->capture("load");
  ASSERT_TRUE(load);
  ASSERT_EQ(load->instructions().size(), 1u);
  EXPECT_EQ(load->instructions().front().local_index(), 2);
  const auto empty = match->capture("empty");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->instructions().empty());
  EXPECT_FALSE(match->capture("absent"));
}

TEST_F(InstructionPattern, ImplementsAlternationOverlapAndZeroWidthProgress) {
  add(Instruction::nop());
  add(Instruction::nop());
  add(Instruction::nop());
  const auto two = seq(any(), any());
  EXPECT_EQ(find_all(file, index, two)->size(), 1u);
  EXPECT_EQ(find_all(file, index, two, {.overlap = true})->size(), 2u);
  EXPECT_EQ(find_first(file, index, either(two, any())).value()->size(), 2u);
  EXPECT_EQ(find_first(file, index, either(any(), two)).value()->size(), 1u);
  EXPECT_EQ(find_first(file, index, either(seq(), any())).value()->size(), 1u);
  EXPECT_TRUE(find_all(file, index, repeat(any(), 0, 0))->empty());
  EXPECT_TRUE(find_all(file, index, either())->empty());
  const auto nullable = repeat(optional(istore()), 2, std::numeric_limits<std::size_t>::max());
  EXPECT_EQ(find_all(file, index, seq(nullable, any()))->size(), 3u);
}

TEST_F(InstructionPattern, GreedyOptionalIterationCanConsumeAfterMandatoryEmptyIterations) {
  const auto id = add(Instruction::nop());
  const auto pattern = repeat(capture("last", either(seq(), any())), 1, 2);
  const auto match = find_first(file, index, pattern).value();
  ASSERT_TRUE(match);
  const auto last = match->capture("last");
  ASSERT_TRUE(last);
  ASSERT_EQ(last->instructions().size(), 1u);
  EXPECT_EQ(last->instructions().front().id(), id);
  const auto empty = repeat(capture("empty", seq()), 1, std::numeric_limits<std::size_t>::max());
  EXPECT_TRUE(find_first(file, index, seq(empty, any()), {.max_work = 100}).value());
}

TEST_F(InstructionPattern, ReportsBadPatternsMethodAndWorkErrors) {
  const auto bad_index = find_all(file, index + 1, any());
  ASSERT_FALSE(bad_index);
  EXPECT_EQ(bad_index.error().kind, MatchErrorKind::InvalidMethod);
  const auto native = file.add_method(0x0109, text("nativeMethod"), text("()V")).value();
  EXPECT_EQ(find_first(file, native, any()).error().kind, MatchErrorKind::NoCode);
  EXPECT_TRUE(find_all(file, index, any())->empty());
  EXPECT_EQ(find_first(file, index, repeat(any(), 3, 2)).error().kind, MatchErrorKind::InvalidPattern);
  EXPECT_EQ(find_first(file, index, seq(capture("x", any()), capture("x", any()))).error().kind,
            MatchErrorKind::InvalidPattern);
  EXPECT_EQ(find_first(file, index, field({.opcode = Opcode::InvokeStatic})).error().kind, MatchErrorKind::InvalidPattern);
  add(Instruction::nop());
  EXPECT_EQ(find_first(file, index, any(), {.max_work = 0}).error().kind, MatchErrorKind::WorkLimit);
  const auto large = seq(repeat(seq(), 5000, 5000), any());
  EXPECT_EQ(find_all(file, index, large, {.max_work = 50}).error().kind, MatchErrorKind::WorkLimit);
}

TEST_F(InstructionPattern, ResolvesOnlyVisitedCandidatesAndReturnsTheirIdsOnErrors) {
  add(MethodInvocation{Opcode::InvokeStatic, "Owner", "run", "()V"});
  // Low-level MethodCode permits raw pool references; the reader validates them on demand.
  const auto invalid = body.append(Instruction::constant(Opcode::InvokeStatic, file.this_class()).value()).value();
  EXPECT_TRUE(find_first(file, index, invoke({}))->has_value());
  const auto search = find_all(file, index, invoke({}));
  ASSERT_FALSE(search);
  EXPECT_EQ(search.error().kind, MatchErrorKind::InvalidOperand);
  EXPECT_EQ(search.error().instruction, invalid);
  EXPECT_EQ(find_all(file, index, opcode(Opcode::InvokeStatic))->size(), 2u);
  const auto bad_load = body.append(Instruction::constant(Opcode::Ldc, CpIndex{65535}).value()).value();
  EXPECT_EQ(find_all(file, index, int_constant()).error().instruction, bad_load);
}

TEST_F(InstructionPattern, RejectsMalformedMemberBehindAnLdcMethodHandle) {
  auto& pool = file.constant_pool();
  const auto invalid = pool.append({CpTag::Fieldref, 0, file.this_class(), file.this_class()}).value();
  const auto handle = pool.append({CpTag::MethodHandle, 2, invalid, {}}).value();
  const auto load = body.append(Instruction::constant(Opcode::Ldc, handle).value()).value();
  const auto result = find_first(file, index, int_constant());
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, MatchErrorKind::InvalidOperand);
  EXPECT_EQ(result.error().instruction, load);
  EXPECT_EQ(find_first(file, index, any()).value()->start(), load);
}

TEST(InstructionPatternConstants, ValidatesDynamicBootstrapRowsAndBoundsCyclicTraversal) {
  auto file = ClassFile::create({55, 0}, "PatternConstants").value();
  auto& pool = file.constant_pool();
  const auto text = [&](std::string_view value) { return pool.append_utf8(value).value(); };
  const auto method = file.add_method(0x0009, text("run"), text("()V")).value();
  auto& body = file.add_code(method, text("Code")).value().get().code();
  const auto type = pool.append({CpTag::NameAndType, 0, text("literal"), text("I")}).value();
  const auto dynamic = pool.append({CpTag::Dynamic, 0, {}, type}).value();
  const auto load = body.append(Instruction::constant(Opcode::Ldc, dynamic).value()).value();
  const auto missing = find_all(file, method, int_constant());
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().kind, MatchErrorKind::InvalidOperand);
  EXPECT_EQ(missing.error().instruction, load);

  const auto signature = pool.append({CpTag::NameAndType, 0, text("bootstrap"), text("()I")}).value();
  const auto ref = pool.append({CpTag::Methodref, 0, file.this_class(), signature}).value();
  const auto handle = pool.append({CpTag::MethodHandle, 6, ref, {}}).value();
  BootstrapMethod row{handle, {}};
  row.arguments.assign(1000, dynamic);
  file.add_attribute(Attribute::bootstrap_methods(text("BootstrapMethods"), std::span{&row, 1})).value();
  const auto end = body.append(Instruction::bipush(42)).value();
  EXPECT_EQ(find_first(file, method, int_constant(42)).value()->start(), end);
  const auto exhausted = find_all(file, method, int_constant(), {.max_work = 20});
  ASSERT_FALSE(exhausted);
  EXPECT_EQ(exhausted.error().kind, MatchErrorKind::WorkLimit);
}

TEST_F(InstructionPattern, ChargesSkippedCaptureScratchAndResultMaterialization) {
  for (int i = 0; i < 10; ++i)
    add(Instruction::nop());
  const auto unused = seq(capture("a", any()), capture("b", any()), capture("c", any()), capture("d", any()));
  const auto pattern = either(any(), unused);
  const MatchOptions limited{.max_work = 80};
  auto first = find_first(file, index, pattern, limited);
  ASSERT_TRUE(first && *first);
  EXPECT_FALSE((*first)->capture("a"));
  EXPECT_EQ(find_all(file, index, pattern, limited).error().kind, MatchErrorKind::WorkLimit);
  EXPECT_EQ(find_all(file, index, pattern)->size(), 10u);
}
} // namespace
} // namespace cafe::pattern
