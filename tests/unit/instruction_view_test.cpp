#include <array>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>
#include <cafe/io/class_reader.hpp>
#include <cafe/io/class_writer.hpp>

namespace cafe {
namespace {

static_assert(std::ranges::forward_range<InstructionViewRange>);
static_assert(std::ranges::borrowed_range<InstructionViewRange>);
static_assert(std::ranges::sized_range<InstructionViewRange>);
static_assert(std::same_as<std::ranges::range_value_t<InstructionViewRange>, InstructionView>);

ClassFile fixture() {
  auto file = ClassFile::create({55, 0}, "ViewTest").value();
  const auto name = file.constant_pool().append_utf8("run").value();
  const auto descriptor = file.constant_pool().append_utf8("()V").value();
  (void)file.add_method(0x0009, name, descriptor).value();
  return file;
}

CodeAttribute& add_body(ClassFile& file) {
  const auto code_name = file.constant_pool().append_utf8("Code").value();
  return file.add_code(0, code_name).value().get();
}

TEST(InstructionViews, DistinguishesInvalidAbsentOpaqueAndEmptyCode) {
  auto file = fixture();
  EXPECT_TRUE(file.instructions(1).empty());
  EXPECT_EQ(file.checked_instructions(1).error(), CodeEditError::InvalidMethod);
  EXPECT_TRUE(file.instructions(0).empty());
  EXPECT_EQ(file.checked_instructions(0).error(), CodeEditError::NoCode);
  EXPECT_EQ(file.instruction({0, {}}).error(), CodeEditError::NoCode);
  add_body(file);
  ASSERT_TRUE(file.checked_instructions(0));
  EXPECT_TRUE(file.checked_instructions(0)->empty());
  EXPECT_EQ(file.instruction({0, {}}).error(), CodeEditError::InvalidInstruction);

  std::ifstream stream(HIPPOCAFE_FIXTURE_DIR "/generated/ReservedOpcode.class", std::ios::binary);
  ASSERT_TRUE(stream);
  const std::string bytes(std::istreambuf_iterator<char>{stream}, {});
  const auto opaque = read_class(std::as_bytes(std::span(bytes)), {.allow_opaque_code = true});
  ASSERT_TRUE(opaque);
  EXPECT_TRUE(opaque->class_file.instructions(0).empty());
  EXPECT_EQ(opaque->class_file.checked_instructions(0).error(), CodeEditError::NoCode);
}

TEST(InstructionViews, TraversesParsedInstructionsInOrderWithNormalizedLocalAndSignedOperands) {
  auto model = fixture();
  auto& body = add_body(model);
  const auto values = std::to_array<InstructionBuilder>({Instruction::bipush(-10), Instruction::istore(0), Instruction::iload(0), Instruction::istore(300),
                                                         Instruction::iload(300), Instruction::pop(), Instruction::sipush(-600), Instruction::pop(), Instruction::return_void()});
  ASSERT_TRUE(body.code().append(values));
  body.supply_metadata(1, 301, {});
  const auto output = write_class(model, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata});
  ASSERT_TRUE(output);
  auto parsed = read_class(output->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed);
  const auto& file = parsed->class_file;
  const auto views = file.instructions(0);
  EXPECT_EQ(views.size(), values.size());
  std::size_t position = 0;
  for (auto view : views) {
    EXPECT_EQ(view.id(), file.methods()[0].code()->code().at(position));
    EXPECT_EQ(view.location(), (InstructionLocation{0, view.id()}));
    EXPECT_EQ(view.raw(), *file.methods()[0].code()->code().get(view.id()));
    if (position == 0) {
      EXPECT_TRUE(view.is(Opcode::Bipush));
      EXPECT_EQ(view.immediate(), -10);
      EXPECT_EQ(format(view), "bipush -10");
    } else if (position == 6) {
      EXPECT_EQ(view.immediate(), -600);
    } else {
      EXPECT_FALSE(view.immediate());
    }
    if (position == 2 || position == 4) {
      EXPECT_TRUE(view.is(Opcode::ILoad));
      EXPECT_EQ(view.local_index(), position == 2 ? 0 : 300);
      EXPECT_EQ(view.raw().encoding_hint(), position == 2 ? EncodingHint::Implicit : EncodingHint::Wide);
    }
    ++position;
  }
}

TEST(InstructionViews, ResolvesMembersAndRetainsSnapshotsAcrossClassMoveAndCodeEdits) {
  auto source = fixture();
  add_body(source);
  auto editor = source.edit_method(0).value();
  const auto target = editor.append(MethodInvocation{Opcode::InvokeStatic, "java/lang/Math", "abs", "(I)I"}).value();
  const auto field_id = editor.append(FieldAccess{Opcode::GetStatic, "java/lang/System", "out", "Ljava/io/PrintStream;"}).value();
  const auto iface_id = editor.append(MethodInvocation{Opcode::InvokeInterface, "Iface", "run", "()V"}).value();
  const auto clone_id = editor.append(MethodInvocation{Opcode::InvokeVirtual, "[I", "clone", "()Ljava/lang/Object;"}).value();
  const auto range = source.instructions(0);
  auto file = std::move(source);
  const auto call_view = *range.begin();
  EXPECT_TRUE(call_view.is_invoke());
  ASSERT_TRUE(call_view.method_invocation());
  EXPECT_TRUE(call_view.method_invocation()->matches("java/lang/Math", "abs", "(I)I"));
  EXPECT_FALSE(call_view.method_invocation()->matches("java/lang/Math", "abs", "(J)J"));
  EXPECT_EQ(format(call_view), "invokestatic java/lang/Math.abs (I)I");
  EXPECT_EQ(call_view.field_access().error(), CodeEditError::WrongOperandForm);
  const auto field = file.instruction({0, field_id}).value();
  EXPECT_TRUE(field.is_field_access());
  ASSERT_TRUE(field.field_access());
  EXPECT_EQ(field.field_access()->name, "out");
  EXPECT_EQ(field.method_invocation().error(), CodeEditError::WrongOperandForm);
  EXPECT_TRUE(file.instruction({0, iface_id})->method_invocation()->owner_is_interface);
  EXPECT_TRUE(file.instruction({0, clone_id})->method_invocation()->matches("[I", "clone", "()Ljava/lang/Object;"));

  ASSERT_TRUE(editor.insert_before(target, Instruction::nop()));
  EXPECT_EQ(file.instruction(call_view.location())->id(), target);
  ASSERT_TRUE(editor.replace(target, Instruction::pop()));
  EXPECT_TRUE(call_view.method_invocation()->matches("java/lang/Math", "abs", "(I)I"));
  EXPECT_TRUE(file.instruction(call_view.location())->is(Opcode::Pop));
  ASSERT_TRUE(editor.erase(target));
  EXPECT_EQ(file.instruction(call_view.location()).error(), CodeEditError::InvalidInstruction);
}

TEST(InstructionViews, SeparatesMalformedMemberReferencesFromNonmatchingOpcodes) {
  auto file = fixture();
  auto& code = add_body(file).code();
  auto wrong_tag = file.this_class();
  const auto bad_call = code.append(Instruction::constant(Opcode::InvokeStatic, wrong_tag).value()).value();
  const auto bad_field = code.append(Instruction::constant(Opcode::GetStatic, wrong_tag).value()).value();
  const auto indy = code.append(Instruction::constant(Opcode::InvokeDynamic, wrong_tag).value()).value();
  EXPECT_EQ(file.instruction({0, bad_call})->method_invocation().error(), CodeEditError::InvalidOperand);
  EXPECT_EQ(file.instruction({0, bad_field})->field_access().error(), CodeEditError::InvalidOperand);
  const auto dynamic = file.instruction({0, indy}).value();
  EXPECT_FALSE(dynamic.is_invoke());
  EXPECT_EQ(dynamic.method_invocation().error(), CodeEditError::WrongOperandForm);
  EXPECT_EQ(file.inspect_instruction({0, indy}).error(), CodeEditError::InvalidOperand);
}

} // namespace
} // namespace cafe
