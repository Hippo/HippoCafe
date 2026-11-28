#include <array>
#include <string>

#include <gtest/gtest.h>
#include <hippo/cafe.hpp>

namespace cafe {
namespace {

ClassFile one_method(ClassVersion version = {55, 0}) {
  auto file = ClassFile::create(version, "Edited").value();
  auto& pool = file.constant_pool();
  const auto index = file.add_method(0x0009, pool.append_utf8("run").value(), pool.append_utf8("()V").value()).value();
  auto& code = file.add_code(index, pool.append_utf8("Code").value()).value().get().code();
  code.append(Instruction::return_void()).value();
  return file;
}

void fill_pool(ConstantPool& pool) {
  while (pool.size() < UINT16_MAX) {
    const auto bits = pool.size();
    pool.append({.tag = CpTag::Integer, .first = {}, .second = {}, .bits = bits}).value();
  }
}

TEST(SequenceResolution, CompactedRetryRemapsRawOperandsAndRetainsBorrowedText) {
  auto file = one_method();
  auto& pool = file.constant_pool();
  const auto borrowed = pool.append_utf8("borrowed utf8").value();
  const auto raw_text = pool.append_utf8("raw operand").value();
  const auto raw_slot = pool.append({CpTag::String, 0, raw_text, {}}).value();
  const auto raw = Instruction::constant(Opcode::Ldc, raw_slot).value();
  fill_pool(pool);
  auto editor = file.edit_method(0).value();
  auto& code = editor.code()->code();
  const auto ret = code.at(0);
  const auto result = editor.insert_before(ret, {insn::ldc(pool.get_utf8(borrowed).value()), insn::pop(), raw, insn::pop()});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->size(), 4u);
  EXPECT_LT(pool.size(), 20u);
  EXPECT_EQ(code.at(4), ret);
  const auto literal = [&](InstructionId id) {
    const auto index = code.get(id)->constant_index().value();
    return pool.get_utf8(pool.get(index)->first);
  };
  EXPECT_EQ(literal((*result)[0]), "borrowed utf8");
  EXPECT_EQ(literal((*result)[2]), "raw operand");
}

TEST(SequenceResolution, FailedRetryRestoresThePoolAndClassAttributes) {
  auto file = one_method();
  auto& pool = file.constant_pool();
  const auto source = pool.append_utf8("SourceFile").value();
  const auto filename = pool.append_utf8("Edited.java").value();
  ASSERT_TRUE(file.add_attribute(Attribute(source, SourceFile{filename})));
  const auto saved_text = pool.get_utf8(filename).value();
  const auto scope = file.declaration_revision();
  auto editor = file.edit_method(0).value();
  auto& code = editor.code()->code();
  const auto ret = code.at(0);
  while (pool.size() <= UINT8_MAX)
    pool.append_utf8("unused").value();
  const auto live_constant = pool.append({CpTag::String, 0, filename, {}}).value();
  const auto live = Instruction::constant(Opcode::Ldc, live_constant).value();
  const auto load = code.insert_before(ret, live).value();
  code.insert_before(ret, Instruction::pop()).value();
  const auto revision = code.revision();
  fill_pool(pool);
  const Bootstrap bootstrap{{MethodHandleKind::InvokeStatic, "Bootstraps", "link", "()V"}, {}};
  const auto result = editor.insert_before(ret, {DynamicInvocation{"invoke", "()V", bootstrap}, insn::iload(UINT16_MAX)});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().cause, CodeEditError::InvalidOperand);
  EXPECT_EQ(result.error().instruction_index, 1u);
  EXPECT_EQ(pool.size(), UINT16_MAX);
  EXPECT_EQ(pool.get_utf8(filename).value().data(), saved_text.data());
  EXPECT_EQ(file.declaration_revision(), scope);
  EXPECT_EQ(code.revision(), revision);
  EXPECT_EQ(code.size(), 3u);
  EXPECT_EQ(code.get(load), live);
  EXPECT_EQ(code.at(2), ret);
  ASSERT_EQ(file.attributes().size(), 1u);
  EXPECT_EQ(file.attributes().front().name_index(), source);
  EXPECT_EQ(file.attributes().front().get<SourceFile>()->file_name, filename);
}

TEST(SequenceResolution, OpaqueReferencesLeaveTheOriginalPoolLimitError) {
  auto file = one_method();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("PrivateAttribute").value();
  ASSERT_TRUE(file.add_attribute(Attribute::raw(name, {std::byte{0}, std::byte{1}})));
  fill_pool(pool);
  const auto revision = file.declaration_revision();
  auto editor = file.edit_method(0).value();
  const auto ret = editor.code()->code().at(0);
  EXPECT_EQ(editor.insert_before(ret, insn::ldc(0x12345)), std::unexpected(CodeEditError::ConstantPoolLimit));
  EXPECT_EQ(pool.size(), UINT16_MAX);
  EXPECT_EQ(file.declaration_revision(), revision);
  EXPECT_EQ(editor.code()->code().at(0), ret);
  EXPECT_EQ(file.attributes().front().name_index(), name);
}

TEST(SequenceResolution, RetriesSymbolicExceptionHandlerInsertion) {
  auto file = one_method();
  auto editor = file.edit_method(0).value();
  auto& code = editor.code()->code();
  const auto start = code.append(Instruction::nop()).value();
  const auto target = code.append(Instruction::return_void()).value();
  const auto start_label = editor.label_before(start).value();
  const auto target_label = editor.label_before(target).value();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("java/lang/Exception").value();
  fill_pool(pool);
  ASSERT_TRUE(editor.add_exception_handler(start_label, target_label, target_label, pool.get_utf8(name).value()));
  const auto handlers = editor.code()->exception_handlers();
  ASSERT_EQ(handlers.size(), 1u);
  EXPECT_EQ(handlers.front().handler, target_label);
  EXPECT_EQ(pool.get_utf8(pool.get(handlers.front().catch_type)->first), "java/lang/Exception");
  EXPECT_LT(pool.size(), 20u);
}

TEST(SequenceResolution, ValidatesNestedReferencesAndRollsBackEarlierOperands) {
  auto file = one_method();
  auto editor = file.edit_method(0).value();
  auto& body = editor.code()->code();
  auto& pool = file.constant_pool();
  const auto bad_name = pool.append_utf8("bad.class.Name").value();
  const auto bad_owner = pool.append({CpTag::Class, 0, bad_name, {}}).value();
  const auto name = pool.append_utf8("value").value();
  const auto descriptor = pool.append_utf8("I").value();
  const auto field_type = pool.append({CpTag::NameAndType, 0, name, descriptor}).value();
  const auto field = pool.append({CpTag::Fieldref, 0, bad_owner, field_type}).value();
  const auto read = Instruction::constant(Opcode::GetStatic, field).value();
  const auto anchor = body.at(0);
  const auto revision = body.revision();
  const auto previous = pool.size();
  const auto result = editor.insert_before(anchor, {MethodInvocation{Opcode::InvokeStatic, "NewOwner", "abs", "(I)I"}, read});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().cause, CodeEditError::InvalidOperand);
  EXPECT_EQ(result.error().instruction_index, 1u);
  EXPECT_EQ(body.revision(), revision);
  EXPECT_EQ(body.size(), 1u);
  EXPECT_EQ(body.at(0), anchor);
  EXPECT_EQ(pool.size(), previous);
  EXPECT_EQ(editor.replace(anchor, read), std::unexpected(CodeEditError::InvalidOperand));
}

TEST(SequenceResolution, ValidatesReachableRawBootstrapRowsWithoutCreatingMetadata) {
  auto file = one_method();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("dynamicCall").value();
  const auto desc = pool.append_utf8("()V").value();
  const auto signature = pool.append({CpTag::NameAndType, 0, name, desc}).value();
  const auto site = pool.append({CpTag::InvokeDynamic, 0, {}, signature}).value();
  const auto call = Instruction::constant(Opcode::InvokeDynamic, site).value();
  auto editor = file.edit_method(0).value();
  const auto revision = editor.code()->code().revision();
  EXPECT_EQ(editor.append(call), std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_TRUE(file.attributes().empty());

  const auto bad_handle = pool.append({CpTag::MethodHandle, 6, signature, {}}).value();
  const std::array rows{BootstrapMethod{bad_handle, {}}};
  file.add_attribute(Attribute::bootstrap_methods(pool.append_utf8("BootstrapMethods").value(), rows)).value();
  EXPECT_EQ(editor.append(call), std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_EQ(file.attributes().front().get<BootstrapMethods>()->methods.size(), 1u);
  EXPECT_EQ(editor.code()->code().revision(), revision);
}

TEST(SequenceResolution, ChecksRepresentableLocalWidthIndependentlyOfDeclaredMaxima) {
  auto file = one_method();
  auto editor = file.edit_method(0).value();
  ASSERT_TRUE(editor.insert_before(editor.code()->code().at(0), Instruction::lload(65533)));
  EXPECT_EQ(editor.append(Instruction::lload(65534)), std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_EQ(editor.append(Instruction::iload(65535)), std::unexpected(CodeEditError::InvalidOperand));

  auto historical = one_method({45, 2});
  auto legacy = historical.edit_method(0).value();
  ASSERT_TRUE(legacy.append(Instruction::iload(254)));
  EXPECT_EQ(legacy.append(Instruction::iload(255)), std::unexpected(CodeEditError::InvalidOperand));
}

TEST(SequenceResolution, CountsTheReceiverWhenValidatingRawInvocations) {
  auto file = one_method();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("many").value();
  const auto descriptor = pool.append_utf8("(" + std::string(255, 'I') + ")V").value();
  const auto signature = pool.append({CpTag::NameAndType, 0, name, descriptor}).value();
  const auto target = pool.append({CpTag::Methodref, 0, file.this_class(), signature}).value();
  auto editor = file.edit_method(0).value();
  EXPECT_EQ(editor.append(Instruction::constant(Opcode::InvokeVirtual, target).value()),
            std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_TRUE(editor.append(Instruction::constant(Opcode::InvokeStatic, target).value()));
}

TEST(SequenceResolution, RawAndSymbolicMethodHandleLoadsRespectReceiverSlots) {
  auto file = one_method();
  auto editor = file.edit_method(0).value();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("target").value();
  for (const auto count : {254u, 255u}) {
    const auto text = "(" + std::string(count, 'I') + ")V";
    const auto signature = pool.append({CpTag::NameAndType, 0, name, pool.append_utf8(text).value()}).value();
    const auto target = pool.append({CpTag::Methodref, 0, file.this_class(), signature}).value();
    for (const auto kind : {MethodHandleKind::InvokeVirtual, MethodHandleKind::InvokeStatic}) {
      SCOPED_TRACE(count);
      SCOPED_TRACE(static_cast<unsigned>(kind));
      const auto handle = pool.append({CpTag::MethodHandle, static_cast<std::uint8_t>(kind), target, {}}).value();
      const auto revision = editor.code()->code().revision();
      const auto raw = editor.append(Instruction::constant(Opcode::Ldc, handle).value());
      const auto symbolic = editor.append(insn::ldc(MethodHandle{kind, "Edited", "target", text}));
      if (count == 255 && kind == MethodHandleKind::InvokeVirtual) {
        EXPECT_EQ(raw, std::unexpected(CodeEditError::InvalidOperand));
        EXPECT_EQ(symbolic, std::unexpected(CodeEditError::InvalidOperand));
        EXPECT_EQ(editor.code()->code().revision(), revision);
      } else {
        EXPECT_TRUE(raw);
        EXPECT_TRUE(symbolic);
      }
    }
  }
}

} // namespace
} // namespace cafe
