#include <array>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <hippo/cafe.hpp>

namespace cafe {
namespace {

ClassFile builder_fixture() {
  auto file = ClassFile::create({55, 0}, "BuilderFixture").value();
  auto& pool = file.constant_pool();
  const auto name = pool.append_utf8("run").value();
  const auto method = file.add_method(0x0009, name, pool.append_utf8("()V").value()).value();
  auto& code = file.add_code(method, pool.append_utf8("Code").value()).value().get().code();
  code.append(Instruction::return_void()).value();
  return file;
}

TEST(CodeBuilder, ReplaysMovedSequencesWithMethodLocalForwardBackwardAndTrailingTargets) {
  CodeBuilder source;
  const auto loop = source.label();
  const auto end = source.label();
  source.bind(loop).iconst_0().ifeq(end);
  CodeBuilder patch(std::move(source));
  patch.ldc(std::string("temporary")).pop().goto_(loop).bind(end);

  for (int iteration = 0; iteration < 2; ++iteration) {
    auto file = builder_fixture();
    if (iteration)
      file.constant_pool().append_utf8("different pool layout").value();
    auto editor = file.edit_method(0).value();
    const auto& code = editor.code()->code();
    const auto anchor = code.at(0);
    const auto revision = code.revision();
    const auto trailing = editor.insert_after(anchor, patch);
    ASSERT_FALSE(trailing);
    EXPECT_EQ(code.revision(), revision);
    EXPECT_EQ(code.size(), 1u);
    const auto result = editor.insert_before(anchor, patch);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 5u);
    EXPECT_EQ(code.label_position(code.branch_target((*result)[1]).value())->before, anchor);
    EXPECT_EQ(code.label_position(code.branch_target((*result)[4]).value())->before, (*result)[0]);
    const auto literal = code.get((*result)[2])->constant_index().value();
    const auto* constant = file.constant_pool().get(literal);
    ASSERT_TRUE(constant);
    EXPECT_EQ(file.constant_pool().get_utf8(constant->first), "temporary");
  }
}

TEST(CodeBuilder, OwnsNamesNestedBootstrapArgumentsAndSwitchArraysAtEmission) {
  auto file = builder_fixture();
  auto editor = file.edit_method(0).value();
  auto& code = editor.code()->code();
  const auto anchor = code.at(0);
  const auto target = code.make_label().value();
  code.bind_label(target, CodePosition{anchor}).value();
  CodeBuilder patch;
  {
    std::string owner = "mutable/Target";
    std::string text = "original text";
    std::string array_type = "[Lmutable/Target;";
    const MethodHandle handle{MethodHandleKind::InvokeStatic, owner, "bootstrap", "()I"};
    const std::array nested{BootstrapArgument{std::string_view(text)}};
    const std::array arguments{BootstrapArgument{DynamicConstant{"child", "I", {handle, nested}}}};
    std::array dense{target};
    std::array sparse{LookupPair{5, target}};
    patch.invokestatic(owner, "call", "()V")
        .checkcast(array_type)
        .invokedynamic("call", "()V", {handle, arguments})
        .tableswitch(3, target, dense)
        .lookupswitch(target, sparse);
    owner.assign(owner.size(), 'x');
    text.assign(text.size(), 'x');
    array_type.assign(array_type.size(), 'x');
    dense.front() = {};
    sparse.front() = {};
  }
  const auto result = editor.insert_before(anchor, patch);
  ASSERT_TRUE(result);
  const auto static_call = file.inspect_instruction({0, (*result)[0]}).value();
  EXPECT_EQ(std::get<MethodInvocation>(static_call.operand()).owner, "mutable/Target");
  const auto class_index = code.get((*result)[1])->constant_index().value();
  EXPECT_EQ(file.constant_pool().get_utf8(file.constant_pool().get(class_index)->first), "[Lmutable/Target;");
  const auto call = file.inspect_instruction({0, (*result)[2]}).value();
  const auto& bootstrap = std::get<DynamicInvocation>(call.operand()).bootstrap;
  ASSERT_EQ(bootstrap.arguments.size(), 1u);
  EXPECT_EQ(bootstrap.handle.owner, "mutable/Target");
  const auto& child = std::get<DynamicConstant>(bootstrap.arguments.front().value);
  ASSERT_EQ(child.bootstrap.arguments.size(), 1u);
  EXPECT_EQ(std::get<std::string_view>(child.bootstrap.arguments.front().value), "original text");
  EXPECT_EQ(code.table_switch(code.switch_id((*result)[3]).value())->targets.front(), target);
  EXPECT_EQ(code.lookup_switch(code.switch_id((*result)[4]).value())->pairs.front(), (LookupPair{5, target}));
}

TEST(CodeBuilder, ReportsFirstConstructionErrorWithoutPublishingEarlierOperands) {
  auto file = builder_fixture();
  auto editor = file.edit_method(0).value();
  const auto& code = editor.code()->code();
  const auto revision = code.revision();
  const auto pool_size = file.constant_pool().size();
  const auto anchor = code.at(0);
  CodeBuilder unrelated;
  const auto foreign = unrelated.label();
  const auto misplaced = editor.insert_before(anchor, [&](auto& pending) {
    pending.ldc("unpublished").goto_(foreign).bipush(1000);
  });
  ASSERT_FALSE(misplaced);
  EXPECT_EQ(misplaced.error().cause, CodeEditError::InvalidLabel);
  EXPECT_EQ(misplaced.error().instruction_index, 1u);
  const auto invalid = editor.insert_before(anchor, [](auto& pending) {
    pending.nop().bipush(256);
  });
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().cause, CodeEditError::InvalidOperand);
  EXPECT_EQ(invalid.error().instruction_index, 1u);
  const auto twice = editor.insert_before(anchor, [](auto& pending) {
    const auto marker = pending.label();
    pending.bind(marker).nop().bind(marker);
  });
  ASSERT_FALSE(twice);
  EXPECT_EQ(twice.error().cause, CodeEditError::InvalidLabel);
  CodeBuilder forward;
  const auto label = forward.label();
  forward.goto_(label);
  const auto missing = editor.insert_before(anchor, forward);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().cause, CodeEditError::UnboundLabel);
  EXPECT_EQ(code.revision(), revision);
  EXPECT_EQ(code.size(), 1u);
  EXPECT_EQ(file.constant_pool().size(), pool_size);
  forward.bind(label);
  EXPECT_TRUE(editor.insert_before(anchor, forward));
}

TEST(CodeBuilder, OwnsCyclicBootstrapGraphsAndReportsTheEditError) {
  CodeBuilder patch;
  {
    const MethodHandle handle{MethodHandleKind::InvokeStatic, "Owner", "link", "()I"};
    std::array<BootstrapArgument, 1> cycle;
    cycle.front().value = DynamicConstant{"child", "I", {handle, cycle}};
    patch.ldc(DynamicConstant{"root", "I", {handle, cycle}});
  }
  auto file = builder_fixture();
  auto editor = file.edit_method(0).value();
  const auto& code = editor.code()->code();
  const auto pool_size = file.constant_pool().size();
  const auto result = editor.insert_before(code.at(0), patch);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().cause, CodeEditError::InvalidOperand);
  EXPECT_EQ(code.size(), 1u);
  EXPECT_EQ(file.constant_pool().size(), pool_size);
  EXPECT_TRUE(file.attributes().empty());
}

} // namespace
} // namespace cafe
