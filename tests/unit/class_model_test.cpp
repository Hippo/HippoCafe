#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <hippo/cafe.hpp>

#include "io/binary_writer.hpp"

namespace cafe {
namespace {

const ReadOptions STRICT{.mode = ParseMode::Strict};
const WriteOptions PRESERVE{.constant_pool = ConstantPoolWritePolicy::Preserve};
const WriteOptions SUPPLIED{.code_policy = CodeWritePolicy::CallerSuppliedMetadata};
const WriteOptions SUPPLIED_PRESERVE{.code_policy = CodeWritePolicy::CallerSuppliedMetadata,
                                     .constant_pool = ConstantPoolWritePolicy::Preserve};

ClassFile fixture(ClassVersion version = {52, 0}) {
  return ClassFile::create(version, "Fixture").value();
}

CpIndex text(ClassFile& file, std::string_view value) {
  return file.constant_pool().append_utf8(value).value();
}

CodeAttribute& with_return(ClassFile& file, std::string_view method_name = "run") {
  const auto name = text(file, method_name);
  const auto descriptor = text(file, "()V");
  const auto code_name = text(file, "Code");
  const auto method = file.add_method(0x0009, name, descriptor).value();
  auto& code = file.add_code(method, code_name).value().get();
  (void)code.code().append(Instruction::simple(Opcode::Return).value()).value();
  code.supply_metadata(0, 0, {});
  return code;
}

class CountingResource final : public std::pmr::memory_resource {
public:
  std::size_t live = 0;
  std::optional<std::size_t> allocations_left;

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (allocations_left && *allocations_left == 0)
      throw std::bad_alloc();
    if (allocations_left)
      --*allocations_left;
    auto* value = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    return value;
  }

  void do_deallocate(void* value, std::size_t bytes, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
    live -= bytes;
  }

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return &other == this;
  }
};

constexpr Bootstrap TEST_BOOTSTRAP{{MethodHandleKind::InvokeStatic, "BootstrapOwner", "link",
                                    "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                                    "Ljava/lang/Class;)Ljava/lang/Object;"}};

TEST(ClassModel, AddsSymbolicDeclarationsAndHandlersTransactionally) {
  auto file = fixture({49, 0});
  const auto initial_pool_size = file.constant_pool().size();
  EXPECT_EQ(file.add_method(0x0009, "broken", "bad").error(), ModelError::InvalidReference);
  EXPECT_EQ(file.add_field(0x0001, "broken", "V").error(), ModelError::InvalidReference);
  EXPECT_EQ(file.add_interface("[Ljava/lang/Runnable;"), std::unexpected(ModelError::InvalidReference));
  EXPECT_EQ(file.constant_pool().size(), initial_pool_size);
  ASSERT_TRUE(file.add_interface("java/lang/Runnable"));
  ASSERT_TRUE(file.add_field(0x0001, "count", "I"));

  auto editor = file.add_method(0x0001, "run", "()V").value();
  ASSERT_TRUE(editor.add_code());
  EXPECT_EQ(editor.add_code().error(), ModelError::DuplicateCode);
  const auto load = editor.append(insn::aconst_null()).value();
  ASSERT_TRUE(editor.append(insn::athrow()));
  const auto pop = editor.append(insn::pop()).value();
  ASSERT_TRUE(editor.append(Instruction::return_void()));
  const auto start = editor.label_before(load).value();
  const auto handler = editor.label_before(pop).value();
  const auto end = editor.label_at_end().value();
  const auto pool_before_handlers = file.constant_pool().size();
  EXPECT_EQ(editor.add_exception_handler(handler, start, handler, "java/lang/Exception"),
            std::unexpected(CodeEditError::InvalidLabel));
  EXPECT_EQ(editor.add_exception_handler(start, end, end), std::unexpected(CodeEditError::InvalidLabel));
  EXPECT_EQ(editor.add_exception_handler(start, handler, handler, "[Ljava/lang/Exception;"),
            std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_EQ(file.constant_pool().size(), pool_before_handlers);
  EXPECT_TRUE(editor.code()->exception_handlers().empty());
  ASSERT_TRUE(editor.add_exception_handler(start, handler, handler, "java/lang/Exception"));
  ASSERT_TRUE(editor.add_exception_handler(start, handler, handler));
  EXPECT_FALSE(file.edit_method("run", "()I"));

  auto relocated = std::move(file);
  const auto matches = editor.find_all(pattern::opcode(Opcode::Pop)).value();
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches.front().start(), pop);
  const auto output = write_class(relocated, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(output) << output.error().detail;
  auto decoded = read_class(output->bytes, STRICT).value();
  const auto decoded_editor = decoded.class_file.edit_method("run", "()V").value();
  const auto handlers = decoded_editor.code()->exception_handlers();
  ASSERT_EQ(handlers.size(), 2u);
  const auto& pool = decoded.class_file.constant_pool();
  EXPECT_EQ(pool.get_utf8(pool.get(handlers[0].catch_type)->first), "java/lang/Exception");
  EXPECT_EQ(handlers[1].catch_type, CpIndex{});
}

TEST(SymbolicEdit, InsertsReplacesAndErasesWithScopedHandles) {
  auto model = fixture({55, 0});
  auto& code = with_return(model).code();
  auto editor = model.edit_method(0).value();
  const auto previous = code.at(0);
  const auto label = code.make_label().value();
  ASSERT_TRUE(code.bind_label(label, CodePosition{previous}));
  for (const auto opcode : {Opcode::GetField, Opcode::PutField, Opcode::GetStatic, Opcode::PutStatic}) {
    const auto field = editor.insert_before(previous, FieldAccess{opcode, "Owner", "state", "I"}).value();
    EXPECT_EQ(code.get(field)->opcode(), opcode);
    EXPECT_TRUE(model.constant_pool().has(*code.get(field)->constant_index(), CpTag::Fieldref));
    EXPECT_TRUE(editor.erase(field));
  }
  for (const auto opcode : {Opcode::InvokeVirtual, Opcode::InvokeSpecial, Opcode::InvokeStatic, Opcode::InvokeInterface}) {
    const auto call = editor.insert_before(previous, MethodInvocation{opcode, "Owner", "run", "(JD)V"}).value();
    const auto instruction = code.get(call).value();
    const bool interface = opcode == Opcode::InvokeInterface;
    EXPECT_EQ(instruction.opcode(), opcode);
    EXPECT_TRUE(model.constant_pool().has(*instruction.constant_index(), interface ? CpTag::InterfaceMethodref : CpTag::Methodref));
    if (interface) {
      EXPECT_EQ(instruction.interface_count(), 5);
    }
    EXPECT_TRUE(editor.erase(call));
  }

  const auto call = editor.insert_at(label, MethodInvocation{Opcode::InvokeStatic, "Iface", "f", "()V", true}).value();
  EXPECT_EQ(code.label_position(label)->before, call);
  EXPECT_TRUE(model.constant_pool().has(*code.get(call)->constant_index(), CpTag::InterfaceMethodref));
  EXPECT_EQ(editor.erase(call), std::unexpected(CodeEditError::LabeledInstruction));
  const auto pool_size = model.constant_pool().size();
  ASSERT_TRUE(editor.replace(call, FieldAccess{Opcode::GetStatic, "Owner", "state", "I"}));
  EXPECT_EQ(model.constant_pool().size(), pool_size);
  EXPECT_EQ(code.get(call)->opcode(), Opcode::GetStatic);
  EXPECT_EQ(code.label_position(label)->before, call);
  ASSERT_TRUE(code.bind_label(label, CodePosition{previous}));
  EXPECT_TRUE(editor.erase(call));
  EXPECT_EQ(editor.erase(call), std::unexpected(CodeEditError::InvalidInstruction));
}

TEST(SymbolicEdit, ReusesRowsAndResolvesNestedBootstrapArguments) {
  auto model = fixture({55, 0});
  auto& code = with_return(model).code();
  auto editor = model.edit_method(0).value();
  const auto anchor = code.at(0);
  const std::array arguments{
      BootstrapArgument{DynamicConstant{"nested", "I", TEST_BOOTSTRAP}},
      BootstrapArgument{std::string_view("text")},
      BootstrapArgument{std::int64_t{42}},
      BootstrapArgument{ClassConstant{"[I"}},
      BootstrapArgument{MethodTypeConstant{"(I)V"}},
      BootstrapArgument{MethodHandle{MethodHandleKind::GetStatic, "Owner", "state", "I"}},
  };
  const Bootstrap bootstrap{TEST_BOOTSTRAP.handle, arguments};
  const DynamicConstant outer{"wide", "J", bootstrap};
  const auto wide = editor.insert_before(anchor, outer).value();
  EXPECT_EQ(code.get(wide)->opcode(), Opcode::Ldc2W);
  const auto size = model.constant_pool().size();
  const auto duplicate = editor.insert_before(anchor, outer).value();
  EXPECT_EQ(code.get(wide)->constant_index(), code.get(duplicate)->constant_index());
  EXPECT_EQ(model.constant_pool().size(), size);

  const auto call = editor.insert_before(anchor, DynamicInvocation{"call", "()V", bootstrap}).value();
  const auto& pool = model.constant_pool();
  const auto* table = model.attributes().front().get<BootstrapMethods>();
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->methods.size(), 2);
  const auto& row = table->methods.back();
  const std::array tags{CpTag::Dynamic, CpTag::String, CpTag::Long, CpTag::Class, CpTag::MethodType, CpTag::MethodHandle};
  ASSERT_EQ(row.arguments.size(), tags.size());
  for (std::size_t i = 0; i < tags.size(); ++i)
    EXPECT_TRUE(pool.has(row.arguments[i], tags[i]));
  EXPECT_EQ(pool.get(*code.get(wide)->constant_index())->first.value, 1);
  EXPECT_EQ(pool.get(*code.get(call)->constant_index())->first.value, 1);
  const auto final_size = pool.size();
  EXPECT_TRUE(editor.erase(wide));
  EXPECT_TRUE(editor.erase(duplicate));
  EXPECT_TRUE(editor.erase(call));
  EXPECT_EQ(pool.size(), final_size);
  EXPECT_EQ(table->methods.size(), 2);
}

TEST(SymbolicEdit, FailedEditsLeaveThePoolCodeAndBootstrapTableUnchanged) {
  auto model = fixture({55, 0});
  auto& code = with_return(model).code();
  auto editor = model.edit_method(0).value();
  const auto pool_size = model.constant_pool().size();
  const auto revision = code.revision();
  const DynamicConstant constant{"name", "I", TEST_BOOTSTRAP};
  EXPECT_EQ(editor.insert_before({}, constant), std::unexpected(CodeEditError::InvalidInstruction));
  EXPECT_EQ(editor.append(DynamicConstant{"name", "V", TEST_BOOTSTRAP}), std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_EQ(editor.append(FieldAccess{Opcode::GetField, "OtherOwner", "state", "(I)V"}),
            std::unexpected(CodeEditError::InvalidOperand));

  std::array<BootstrapArgument, 1> cyclic{};
  cyclic[0].value = DynamicConstant{"cycle", "I", {TEST_BOOTSTRAP.handle, cyclic}};
  EXPECT_EQ(editor.append(DynamicInvocation{"call", "()V", {TEST_BOOTSTRAP.handle, cyclic}}),
            std::unexpected(CodeEditError::InvalidOperand));
  EXPECT_EQ(model.constant_pool().size(), pool_size);
  EXPECT_TRUE(model.attributes().empty());
  EXPECT_EQ(code.revision(), revision);

  const auto anchor = code.at(0);
  ASSERT_TRUE(editor.insert_before(anchor, constant));
  ASSERT_EQ(model.attributes().size(), 1);
  const auto cp_after_success = model.constant_pool().size();
  const auto revision_after_success = code.revision();
  const std::array arguments{BootstrapArgument{std::int32_t{3}}};
  EXPECT_EQ(editor.replace({}, DynamicInvocation{"newCall", "()V", {TEST_BOOTSTRAP.handle, arguments}}),
            std::unexpected(CodeEditError::InvalidInstruction));
  EXPECT_EQ(model.constant_pool().size(), cp_after_success);
  EXPECT_EQ(code.revision(), revision_after_success);
  EXPECT_EQ(model.attributes().front().get<BootstrapMethods>()->methods.size(), 1);
}

TEST(SymbolicEdit, RequiresDecodedCodeAndSupportedClassVersions) {
  auto model = fixture({51, 0});
  with_return(model);
  auto editor = model.edit_method(0).value();
  EXPECT_EQ(editor.append(DynamicConstant{"name", "I", TEST_BOOTSTRAP}),
            std::unexpected(CodeEditError::UnsupportedClassVersion));
  EXPECT_EQ(editor.append(MethodInvocation{Opcode::InvokeSpecial, "Iface", "run", "()V", true}),
            std::unexpected(CodeEditError::UnsupportedClassVersion));
  const auto native = model.add_method(0x0109, text(model, "nativeMethod"), text(model, "()V")).value();
  EXPECT_EQ(model.edit_method(native)->append(Instruction::nop()), std::unexpected(CodeEditError::NoCode));
}

TEST(SymbolicEdit, AllocationFailuresRollBackAndAllowARetry) {
  std::optional<std::vector<std::byte>> expected_output;
  for (std::size_t allowance = 0;; ++allowance) {
    ASSERT_LT(allowance, 128);
    SCOPED_TRACE(allowance);
    CountingResource resource;
    auto model = ClassFile::create({55, 0}, "Fixture", "java/lang/Object", 0x0021, &resource).value();
    auto& code = with_return(model).code();
    auto editor = model.edit_method(0).value();
    const auto anchor = code.at(0);
    const auto entry = code.make_label().value();
    code.bind_label(entry, {anchor}).value();
    const auto jump = editor.insert_before(anchor, insn::goto_(entry)).value();
    const auto match = pattern::find_first(model, 0, pattern::opcode(Opcode::Goto)).value().value();
    const WriteOptions computed{.code_analysis = CodeAnalysisMode::ComputeFrames};
    const auto before = write_class(model, computed).value().bytes;
    const auto pool_size = model.constant_pool().size();
    const auto revision = code.revision();
    const auto apply = [&] {
      return editor.insert_before(anchor, {DynamicConstant{"name", "I", TEST_BOOTSTRAP}, insn::pop()},
                                  InsertionEntry::IncludeIncomingBranches);
    };
    resource.allocations_left = allowance;
    bool injected = false;
    try {
      ASSERT_TRUE(apply());
    } catch (const std::bad_alloc&) {
      resource.allocations_left.reset();
      injected = true;
      EXPECT_EQ(model.constant_pool().size(), pool_size);
      EXPECT_EQ(code.revision(), revision);
      EXPECT_TRUE(model.attributes().empty());
      EXPECT_EQ(code.branch_target(jump).value(), entry);
      EXPECT_EQ(code.label_position(entry).value().before, anchor);
      EXPECT_EQ(write_class(model, computed).value().bytes, before);
      EXPECT_EQ(editor.replace(match, {insn::getstatic("Bad", "state", "()V")}).error().cause,
                CodeEditError::InvalidOperand);
      ASSERT_TRUE(apply());
    }
    resource.allocations_left.reset();
    ASSERT_EQ(model.attributes().size(), 1u);
    EXPECT_EQ(model.attributes().front().get<BootstrapMethods>()->methods.size(), 1u);
    EXPECT_EQ(code.label_position(code.branch_target(jump).value()).value().before, code.at(1));
    EXPECT_EQ(code.label_position(entry).value().before, anchor);
    EXPECT_EQ(editor.erase(match).error().cause, CodeEditError::StaleMatch);
    const auto after = write_class(model, computed).value().bytes;
    if (!expected_output)
      expected_output = after;
    EXPECT_EQ(after, *expected_output);
    if (!injected)
      break;
  }
}

TEST(SymbolicRead, FindsExactReferencesAndReturnsEditableLocations) {
  auto model = fixture({55, 0});
  with_return(model, "first");
  with_return(model, "second");
  const MethodInvocation target{Opcode::InvokeStatic, "api/Tools", "run", "()V", true};
  const FieldAccess field{Opcode::GetStatic, "api/Tools", "state", "I"};
  for (const auto index : {0u, 1u}) {
    auto editor = model.edit_method(index).value();
    const auto anchor = editor.code()->code().at(0);
    ASSERT_TRUE(editor.insert_before(anchor, target));
    ASSERT_TRUE(editor.insert_before(anchor, field));
    ASSERT_TRUE(editor.insert_before(anchor, Instruction::pop()));
    editor.code()->supply_metadata(1, 0, {});
  }
  auto first = model.edit_method(0).value();
  const auto end = first.code()->code().at(3);
  ASSERT_TRUE(first.insert_before(end, Instruction::bipush(7)));
  ASSERT_TRUE(first.insert_before(end, MethodInvocation{Opcode::InvokeStatic, "api/Tools", "run", "(I)V", true}));
  first.code()->supply_metadata(1, 0, {});

  const auto output = write_class(model, SUPPLIED);
  ASSERT_TRUE(output);
  auto parsed = read_class(output->bytes, STRICT);
  ASSERT_TRUE(parsed);
  auto& file = parsed->class_file;
  const auto calls = file.find_invocations("api/Tools", "run", "()V").value();
  ASSERT_EQ(calls.size(), 2);
  EXPECT_EQ(calls[0], (InstructionLocation{0, file.methods()[0].code()->code().at(0)}));
  EXPECT_EQ(calls[1], (InstructionLocation{1, file.methods()[1].code()->code().at(0)}));
  EXPECT_EQ(file.find_invocations("api/Tools", "run", "()V", 1).value(), (std::vector{calls[1]}));
  EXPECT_EQ(file.find_invocations("api/Tools", "run", "(I)V")->size(), 1);
  EXPECT_TRUE(file.find_invocations("other/Tools", "run", "()V")->empty());
  EXPECT_TRUE(file.find_invocations("api/Tools", "missing", "()V")->empty());

  const auto call_read = file.inspect_instruction(calls[0]);
  ASSERT_TRUE(call_read);
  const auto& call = std::get<MethodInvocation>(call_read->operand());
  EXPECT_EQ(call.opcode, Opcode::InvokeStatic);
  EXPECT_EQ(call.owner, "api/Tools");
  EXPECT_EQ(call.name, "run");
  EXPECT_EQ(call.descriptor, "()V");
  EXPECT_TRUE(call.owner_is_interface);
  const auto accesses = file.find_field_accesses("api/Tools", "state", "I").value();
  ASSERT_EQ(accesses.size(), 2);
  EXPECT_TRUE(file.find_field_accesses("api/Tools", "state", "J")->empty());
  const auto field_read = file.inspect_instruction(accesses[0]);
  ASSERT_TRUE(field_read);
  const auto& access = std::get<FieldAccess>(field_read->operand());
  EXPECT_EQ(access.opcode, Opcode::GetStatic);
  EXPECT_EQ(access.owner, "api/Tools");
  EXPECT_EQ(access.name, "state");
  EXPECT_EQ(access.descriptor, "I");

  auto editor = file.edit_method(accesses[1].method_index).value();
  ASSERT_TRUE(editor.replace(calls[1].id, call_read->operand()));
  ASSERT_TRUE(editor.erase(calls[1].id));
  EXPECT_EQ(file.find_invocations("api/Tools", "run", "()V").value(), (std::vector{calls[0]}));
  EXPECT_EQ(file.inspect_instruction(calls[1]).error(), CodeEditError::InvalidInstruction);
}

TEST(SymbolicRead, DecodesTypedNestedBootstrapArgumentsAndKeepsViewsAcrossEdits) {
  auto model = fixture({55, 0});
  auto& code = with_return(model);
  auto editor = model.edit_method(0).value();
  const auto anchor = code.code().at(0);
  constexpr std::string_view STRING = "embedded\xc0\x80";
  const std::array leaves{
      BootstrapArgument{std::int32_t{-12}},
      BootstrapArgument{std::bit_cast<float>(std::uint32_t{0x8000'0000})},
      BootstrapArgument{std::int64_t{-42}},
      BootstrapArgument{3.5},
      BootstrapArgument{STRING},
      BootstrapArgument{ClassConstant{"[I"}},
      BootstrapArgument{MethodTypeConstant{"(I)V"}},
      BootstrapArgument{MethodHandle{MethodHandleKind::InvokeStatic, "api/Tools", "run", "()V", true}},
  };
  const Bootstrap shared{TEST_BOOTSTRAP.handle, leaves};
  const std::array children{
      BootstrapArgument{DynamicConstant{"left", "I", shared}},
      BootstrapArgument{DynamicConstant{"right", "J", shared}},
  };
  const Bootstrap root{TEST_BOOTSTRAP.handle, children};
  ASSERT_TRUE(editor.insert_before(anchor, DynamicInvocation{"call", "()V", root}));
  ASSERT_TRUE(editor.insert_before(anchor, DynamicConstant{"root", "J", root}));
  ASSERT_TRUE(editor.insert_before(anchor, Instruction::pop2()));
  code.supply_metadata(2, 0, {});

  const auto output = write_class(model, SUPPLIED);
  ASSERT_TRUE(output);
  auto parsed = read_class(output->bytes, STRICT);
  ASSERT_TRUE(parsed);
  auto& file = parsed->class_file;
  const auto& body = file.methods()[0].code()->code();
  auto call_read = file.inspect_instruction({0, body.at(0)});
  ASSERT_TRUE(call_read);
  auto moved = std::move(*call_read);
  const auto& call = std::get<DynamicInvocation>(moved.operand());
  EXPECT_EQ(call.name, "call");
  EXPECT_EQ(call.descriptor, "()V");
  EXPECT_EQ(call.bootstrap.handle.owner, TEST_BOOTSTRAP.handle.owner);
  EXPECT_EQ(call.bootstrap.handle.name, TEST_BOOTSTRAP.handle.name);
  EXPECT_EQ(call.bootstrap.handle.descriptor, TEST_BOOTSTRAP.handle.descriptor);
  ASSERT_EQ(call.bootstrap.arguments.size(), 2);
  const auto& left = std::get<DynamicConstant>(call.bootstrap.arguments[0].value);
  const auto& right = std::get<DynamicConstant>(call.bootstrap.arguments[1].value);
  EXPECT_EQ(left.name, "left");
  EXPECT_EQ(right.name, "right");
  EXPECT_EQ(right.descriptor, "J");
  auto args = left.bootstrap.arguments;
  ASSERT_EQ(args.size(), leaves.size());
  EXPECT_EQ(args.data(), right.bootstrap.arguments.data());
  EXPECT_EQ(std::get<std::int32_t>(args[0].value), -12);
  EXPECT_EQ(std::bit_cast<std::uint32_t>(std::get<float>(args[1].value)), 0x8000'0000u);
  EXPECT_EQ(std::get<std::int64_t>(args[2].value), -42);
  EXPECT_EQ(std::get<double>(args[3].value), 3.5);
  EXPECT_EQ(std::get<std::string_view>(args[4].value), STRING);
  EXPECT_EQ(std::get<ClassConstant>(args[5].value).name, "[I");
  EXPECT_EQ(std::get<MethodTypeConstant>(args[6].value).descriptor, "(I)V");
  const auto& handle = std::get<MethodHandle>(args[7].value);
  EXPECT_EQ(handle.kind, MethodHandleKind::InvokeStatic);
  EXPECT_TRUE(handle.owner_is_interface);

  const auto literal = file.inspect_instruction({0, body.at(1)});
  ASSERT_TRUE(literal);
  EXPECT_EQ(std::get<DynamicConstant>(literal->operand()).name, "root");
  auto parsed_editor = file.edit_method(0).value();
  ASSERT_TRUE(parsed_editor.insert_before(body.at(1), DynamicInvocation{"extra", "()V", TEST_BOOTSTRAP}));
  EXPECT_EQ(std::get<std::string_view>(args[4].value), STRING);
  ASSERT_TRUE(parsed_editor.replace(body.at(0), moved.operand()));
}

TEST(SymbolicRead, InspectsCyclesAndRejectsUnrelatedOrInvalidOperands) {
  auto model = fixture({55, 0});
  auto& code = with_return(model);
  auto& pool = model.constant_pool();
  const auto owner = pool.append({CpTag::Class, 0, text(model, "Owner"), {}}).value();
  const auto signature = pool.append({CpTag::NameAndType, 0, text(model, "bootstrap"), text(model, "()I")}).value();
  const auto reference = pool.append({CpTag::Methodref, 0, owner, signature}).value();
  const auto handle = pool.append({CpTag::MethodHandle, 6, reference, {}}).value();
  const auto constant_signature = pool.append({CpTag::NameAndType, 0, text(model, "cycle"), text(model, "I")}).value();
  const auto cycle = pool.append({CpTag::Dynamic, 0, {}, constant_signature}).value();
  const std::array rows{BootstrapMethod{handle, {cycle}}};
  ASSERT_TRUE(model.add_attribute(Attribute::bootstrap_methods(text(model, "BootstrapMethods"), rows)));
  const auto anchor = code.code().at(0);
  ASSERT_TRUE(code.code().insert_before(anchor, Instruction::constant(Opcode::Ldc, cycle).value()));
  ASSERT_TRUE(code.code().insert_before(anchor, Instruction::pop()));
  code.supply_metadata(1, 0, {});

  const auto output = write_class(model, SUPPLIED);
  ASSERT_TRUE(output);
  auto parsed = read_class(output->bytes, STRICT);
  ASSERT_TRUE(parsed);
  auto& file = parsed->class_file;
  const auto& body = file.methods()[0].code()->code();
  const auto inspected = file.inspect_instruction({0, body.at(0)});
  ASSERT_TRUE(inspected);
  const auto& condy = std::get<DynamicConstant>(inspected->operand());
  ASSERT_EQ(condy.bootstrap.arguments.size(), 1);
  const auto& nested = std::get<DynamicConstant>(condy.bootstrap.arguments.front().value);
  EXPECT_EQ(nested.name, condy.name);
  EXPECT_EQ(nested.bootstrap.arguments.data(), condy.bootstrap.arguments.data());

  EXPECT_EQ(file.inspect_instruction({1, {}}).error(), CodeEditError::InvalidMethod);
  EXPECT_EQ(file.inspect_instruction({0, body.at(1)}).error(), CodeEditError::WrongOperandForm);
  EXPECT_EQ(file.find_invocations("Owner", "bootstrap", "()I", 1).error(), CodeEditError::InvalidMethod);
  auto editor = file.edit_method(0).value();
  const auto invalid_field = Instruction::constant(Opcode::GetStatic, cycle).value();
  EXPECT_EQ(editor.append(invalid_field), std::unexpected(CodeEditError::InvalidOperand));
  const auto malformed = editor.code()->code().append(invalid_field).value();
  EXPECT_EQ(file.inspect_instruction({0, malformed}).error(), CodeEditError::InvalidOperand);
  EXPECT_EQ(file.find_field_accesses("Owner", "state", "I").error(), CodeEditError::InvalidOperand);
}

TEST(ConstantPool, PreservesDuplicateIndicesAndBitsAndDecodesModifiedUtf8) {
  auto model = fixture();
  auto& pool = model.constant_pool();
  constexpr std::string_view SPECIAL = "a\xc0\x80\xed\xa0\xbd\xed\xb8\x80";
  const auto first = text(model, SPECIAL);
  const auto second = text(model, SPECIAL);
  ASSERT_NE(first, second);
  const auto saved_view = pool.get_utf8(first).value();

  const CpEntry numbers[] = {
      {.tag = CpTag::Integer, .first = {}, .second = {}, .bits = 0x80000000u},
      {.tag = CpTag::Float, .first = {}, .second = {}, .bits = 0xffa22001u},
      {.tag = CpTag::Long, .first = {}, .second = {}, .bits = 0xfedcba9876543210ull},
      {.tag = CpTag::Double, .first = {}, .second = {}, .bits = 0x7ff61ab123456789ull},
  };
  std::vector<CpIndex> indices;
  for (const auto number : numbers)
    indices.push_back(pool.append(number).value());
  EXPECT_EQ(saved_view, SPECIAL);
  EXPECT_FALSE(pool.get(CpIndex{0}));
  const auto output = write_class(model, PRESERVE);
  ASSERT_TRUE(output) << output.error().detail;
  const auto parsed = read_class(output->bytes, STRICT);
  ASSERT_TRUE(parsed) << parsed.error().detail;
  const auto& roundtrip = parsed->class_file.constant_pool();
  EXPECT_EQ(roundtrip.get_utf8(second), SPECIAL);
  const auto decoded = roundtrip.decode_utf8(first);
  ASSERT_TRUE(decoded);
  constexpr char16_t DECODED[] = u"a\0\U0001F600";
  EXPECT_EQ(*decoded, std::u16string_view(DECODED, 4));
  for (std::size_t i = 0; i < indices.size(); ++i) {
    const auto* entry = roundtrip.get(indices[i]);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->tag, numbers[i].tag);
    EXPECT_EQ(entry->bits, numbers[i].bits);
    if (entry->tag == CpTag::Long || entry->tag == CpTag::Double) {
      EXPECT_EQ(roundtrip.get(CpIndex{static_cast<std::uint16_t>(indices[i].value + 1)}), nullptr);
    }
  }
  EXPECT_EQ(roundtrip.get_utf8(indices[1]).error(), CpError::WrongTag);
  EXPECT_EQ(pool.append_utf8("\xf0\x9f\x98\x80").error(), CpError::InvalidModifiedUtf8);
  EXPECT_EQ(pool.append_utf8(std::string_view("\0", 1)).error(), CpError::InvalidModifiedUtf8);
  EXPECT_EQ(pool.append_utf8("\xc1\x81").error(), CpError::InvalidModifiedUtf8);
}

TEST(ConstantPool, CompactsEquivalentStringsAndKeepsLoadWidths) {
  auto model = fixture();
  auto& code = with_return(model).code();
  const auto anchor = code.at(0);
  auto& pool = model.constant_pool();
  const auto first = pool.append({CpTag::String, 0, text(model, "Hello"), {}}).value();
  for (unsigned i = 0; i < 256; ++i)
    (void)text(model, "unused filler");
  const auto duplicate = pool.append({CpTag::String, 0, text(model, "Hello"), {}}).value();
  const auto other = pool.append({CpTag::String, 0, text(model, "World"), {}}).value();
  for (const auto index : {first, duplicate, other}) {
    ASSERT_TRUE(code.insert_before(anchor, Instruction::constant(Opcode::Ldc, index).value()));
    ASSERT_TRUE(code.insert_before(anchor, Instruction::pop()));
  }

  const auto written = write_class(model, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(written) << written.error().detail;
  const auto reopened = read_class(written->bytes, STRICT);
  ASSERT_TRUE(reopened) << reopened.error().detail;
  const auto& output = reopened->class_file;
  const auto& emitted = output.methods()[0].code()->code();
  const auto load = emitted.get(emitted.at(0)).value();
  const auto wide = emitted.get(emitted.at(2)).value();
  const auto distinct = emitted.get(emitted.at(4)).value();
  EXPECT_EQ(load.constant_index(), wide.constant_index());
  EXPECT_NE(load.constant_index(), distinct.constant_index());
  EXPECT_EQ(load.encoding_hint(), EncodingHint::Explicit);
  EXPECT_EQ(wide.encoding_hint(), EncodingHint::Wide);
  EXPECT_LT(output.constant_pool().size(), 20u);
}

TEST(ConstantPool, AutomaticallyReclaimsSlotsAfterRepeatedReplacements) {
  auto model = fixture();
  auto editor = model.add_method(0x0009, "value", "()Ljava/lang/String;").value();
  ASSERT_TRUE(editor.add_code());
  const auto load = editor.append(insn::ldc(std::string_view("original"))).value();
  const auto ret = editor.append(Instruction::simple(Opcode::AReturn).value()).value();
  const auto label = editor.label_before(ret).value();

  constexpr unsigned REPLACEMENTS = 33'000;
  bool reclaimed = false;
  for (unsigned i = 0; i < REPLACEMENTS; ++i) {
    const auto value = std::to_string(i);
    const auto before = model.constant_pool().size();
    ASSERT_TRUE(editor.replace(load, insn::ldc(std::string_view(value)))) << i;
    reclaimed |= model.constant_pool().size() < before;
  }
  EXPECT_TRUE(reclaimed);
  EXPECT_EQ(editor.code()->code().label_position(label), CodePosition{ret});
  const auto written = write_class(model, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(written) << written.error().detail;
  const auto reopened = read_class(written->bytes, STRICT);
  ASSERT_TRUE(reopened) << reopened.error().detail;
  const auto& emitted_pool = reopened->class_file.constant_pool();
  EXPECT_LT(emitted_pool.size(), 20u);
  const auto& emitted = reopened->class_file.methods()[0].code()->code();
  const auto operand = emitted.get(emitted.at(0))->constant_index().value();
  const auto* constant = emitted_pool.get(operand);
  ASSERT_NE(constant, nullptr);
  EXPECT_EQ(constant->tag, CpTag::String);
  EXPECT_EQ(emitted_pool.get_utf8(constant->first), std::to_string(REPLACEMENTS - 1));
}

TEST(ConstantPool, InMemoryCompactionKeepsCallerSuppliedCodeWidths) {
  auto model = fixture();
  auto editor = model.add_method(0x0009, "value", "()I").value();
  ASSERT_TRUE(editor.add_code());
  for (unsigned i = 0; i < 256; ++i)
    (void)text(model, "unused");
  const auto load = editor.append(insn::ldc(41)).value();
  ASSERT_GT(editor.code()->code().get(load)->constant_index()->value, UINT8_MAX);
  ASSERT_TRUE(editor.append(Instruction::simple(Opcode::IReturn).value()));
  editor.code()->supply_metadata(1, 0, {}, {}, false);

  const auto compacted = model.compact_constant_pool();
  ASSERT_TRUE(compacted) << compacted.error().detail;
  EXPECT_GE(*compacted, 256u);
  EXPECT_LE(editor.code()->code().get(load)->constant_index()->value, UINT8_MAX);
  const auto output = write_class(model, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata,
                                          .preserve_original_encodings = false,
                                          .constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(output) << output.error().detail;
  const auto parsed = read_class(output->bytes, STRICT);
  ASSERT_TRUE(parsed) << parsed.error().detail;
  const auto& code = parsed->class_file.methods()[0].code()->code();
  EXPECT_EQ(code.get(code.at(0))->encoding_hint(), EncodingHint::Wide);
}

TEST(ConstantPool, ChecksUtf8LengthOnlyForEmittedConstants) {
  auto model = fixture();
  auto editor = model.add_method(0x0009, "value", "()Ljava/lang/String;").value();
  ASSERT_TRUE(editor.add_code());
  const std::string oversized(std::size_t{UINT16_MAX} + 1, 'x');
  const auto load = editor.append(insn::ldc(std::string_view(oversized))).value();
  ASSERT_TRUE(editor.append(Instruction::simple(Opcode::AReturn).value()));
  const WriteOptions computed{.code_analysis = CodeAnalysisMode::ComputeMaxs};
  const auto rejected = write_class(model, computed);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, WriteErrorKind::InvalidClassModel);
  EXPECT_EQ(rejected.error().detail, "Utf8 constant length does not fit u2");
  ASSERT_TRUE(editor.replace(load, insn::ldc(std::string_view("short"))));
  EXPECT_TRUE(write_class(model, computed));
}

TEST(ConstantPool, PrunesUnusedBootstrapRowsAndArguments) {
  auto model = fixture({55, 0});
  auto& code = with_return(model).code();
  auto editor = model.edit_method(0).value();
  const auto anchor = code.at(0);
  const std::array unused_args{BootstrapArgument{std::string_view("unused bootstrap argument")}};
  const auto removed = editor.insert_before(
      anchor, DynamicConstant{"discarded", "I", {TEST_BOOTSTRAP.handle, unused_args}}).value();
  ASSERT_TRUE(editor.insert_before(anchor, DynamicConstant{"kept", "I", TEST_BOOTSTRAP}));
  ASSERT_TRUE(editor.insert_before(anchor, Instruction::pop()));
  ASSERT_TRUE(editor.erase(removed));

  const auto written = write_class(model, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(written) << written.error().detail;
  const auto reopened = read_class(written->bytes, STRICT);
  ASSERT_TRUE(reopened) << reopened.error().detail;
  const auto& output = reopened->class_file;
  ASSERT_EQ(output.attributes().size(), 1u);
  ASSERT_EQ(output.attributes()[0].get<BootstrapMethods>()->methods.size(), 1u);
  EXPECT_TRUE(output.attributes()[0].get<BootstrapMethods>()->methods[0].arguments.empty());
  for (std::size_t i = 1; i < output.constant_pool().size(); ++i)
    EXPECT_NE(output.constant_pool().get_utf8(CpIndex{static_cast<std::uint16_t>(i)}), "unused bootstrap argument");

  const auto compacted = model.compact_constant_pool();
  ASSERT_TRUE(compacted) << compacted.error().detail;
  EXPECT_GT(*compacted, 0u);
  ASSERT_EQ(model.attributes()[0].get<BootstrapMethods>()->methods.size(), 1u);
  const auto kept = model.inspect_instruction({0, code.at(0)});
  ASSERT_TRUE(kept);
  EXPECT_EQ(std::get<DynamicConstant>(kept->operand()).name, "kept");
}

TEST(ConstantPool, HandlesUnknownTagsWrongReferencesAndTerminalLong) {
  auto model = fixture();
  const auto output = write_class(model, PRESERVE).value();
  auto bytes = output.bytes;
  bytes[10] = std::byte{99}; // tag of first Utf8
  EXPECT_EQ(read_class(bytes).error().kind, ReadErrorKind::UnknownConstantTag);

  bytes = output.bytes;
  bytes[10] = std::byte{5}; // only one slot left, even though the payload fits
  bytes[8] = std::byte{0};
  bytes[9] = std::byte{2};
  EXPECT_EQ(read_class(bytes).error().kind, ReadErrorKind::InvalidConstantPool);

  CpEntry invalid;
  invalid.tag = CpTag::Class;
  invalid.first = model.this_class();
  ASSERT_TRUE(model.constant_pool().append(invalid));
  EXPECT_EQ(write_class(model, PRESERVE).error().kind, WriteErrorKind::InvalidConstantPoolReference);
}

TEST(ConstantPool, PreservesMalformedUnusedModifiedUtf8WithDiagnostics) {
  auto model = fixture();
  const auto index = text(model, "zyx-unused-utf8");
  auto bytes = write_class(model, PRESERVE).value().bytes;
  constexpr std::array PREFIX{std::byte{'z'}, std::byte{'y'}, std::byte{'x'}};
  const auto found = std::ranges::search(bytes, PREFIX);
  ASSERT_FALSE(found.empty());
  *found.begin() = std::byte{0};
  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::InvalidModifiedUtf8);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible) << compatible.error().detail;
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->class_file.constant_pool().get_utf8(index).error(), CpError::InvalidModifiedUtf8);
  EXPECT_EQ(compatible->class_file.constant_pool().utf8_bytes(index).value().front(), '\0');
  const auto wrote = write_class(compatible->class_file, PRESERVE);
  ASSERT_TRUE(wrote) << wrote.error().detail;
  EXPECT_EQ(wrote->bytes, bytes);
  EXPECT_EQ(wrote->diagnostics.size(), 1u);
}

TEST(ConstantPool, VersionInapplicableTagsHaveControlledWriting) {
  auto model = fixture();
  CpEntry type;
  type.tag = CpTag::MethodType;
  type.first = text(model, "()V");
  ASSERT_TRUE(model.constant_pool().append(type));
  auto bytes = write_class(model, PRESERVE).value().bytes;
  bytes[7] = std::byte{49};
  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::VersionInapplicable);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible) << compatible.error().detail;
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics[0].action, RecoveryAction::RetainedRaw);
  EXPECT_EQ(write_class(compatible->class_file, PRESERVE).error().kind, WriteErrorKind::UnsupportedDialect);
}

TEST(ClassOwnership, MovesSourceWithoutCopyingAndPreservesOpaquePayloadOnAppend) {
  CountingResource upstream;
  {
    auto model = fixture();
    (void)with_return(model);
    const auto name = text(model, "Vendor");
    ASSERT_TRUE(model.add_attribute(Attribute::raw(name, {std::byte{0}, std::byte{4}})));
    auto written = write_class(model, SUPPLIED_PRESERVE).value();
    const auto* source = written.bytes.data();
    auto read = read_class(std::move(written.bytes), {.upstream_resource = &upstream});
    ASSERT_TRUE(read) << read.error().detail;
    auto moved = std::move(read->class_file);
    EXPECT_EQ(moved.source_bytes().data(), source);
    EXPECT_EQ(moved.validity(), ClassValidity::ParsedClean);
    ASSERT_GT(upstream.live, 0u);
    const auto* raw = moved.attributes()[0].get<RawAttribute>();
    ASSERT_NE(raw, nullptr);
    const auto original = raw->bytes(moved).value();
    EXPECT_EQ(original.data(), source + raw->source_slice()->offset);
    (void)text(moved, "appended");
    const auto size = moved.constant_pool().size();
    const auto revision = moved.declaration_revision();
    const auto reclaimed = moved.compact_constant_pool();
    ASSERT_FALSE(reclaimed);
    EXPECT_EQ(reclaimed.error().kind, WriteErrorKind::UnsafeOpaqueAttribute);
    EXPECT_EQ(moved.constant_pool().size(), size);
    EXPECT_EQ(moved.declaration_revision(), revision);
    const auto compact = write_class(moved);
    ASSERT_FALSE(compact);
    EXPECT_EQ(compact.error().kind, WriteErrorKind::UnsafeOpaqueAttribute);
    const auto output = write_class(moved, PRESERVE).value();
    const auto reparsed = read_class(output.bytes, STRICT);
    ASSERT_TRUE(reparsed);
    const auto& after = reparsed->class_file;
    EXPECT_EQ(after.attributes()[0].name_index(), name);
    EXPECT_TRUE(std::ranges::equal(after.attributes()[0].get<RawAttribute>()->bytes(after).value(), original));
    EXPECT_FALSE(after.source_slice(SourceSlice{UINT32_MAX, UINT32_MAX}));
  }
  EXPECT_EQ(upstream.live, 0u);
}

TEST(ClassOwnership, LimitsReclaimParserStorageAfterFailure) {
  auto model = fixture();
  (void)with_return(model);
  const auto bytes = write_class(model, SUPPLIED).value().bytes;
  CountingResource upstream;
  auto options = ReadOptions{.upstream_resource = &upstream};
  options.limits.max_total_allocated_bytes = bytes.size() + 256;
  EXPECT_EQ(read_class(bytes, options).error().kind, ReadErrorKind::ResourceLimitExceeded);
  EXPECT_EQ(upstream.live, 0u);
  options.limits = {};
  options.limits.max_attributes = 0;
  EXPECT_EQ(read_class(bytes, options).error().kind, ReadErrorKind::ResourceLimitExceeded);
  EXPECT_EQ(upstream.live, 0u);
  options.limits = {};
  options.limits.max_methods = 0;
  EXPECT_EQ(read_class(bytes, options).error().kind, ReadErrorKind::ResourceLimitExceeded);
  options.limits = {};
  options.limits.max_code_bytes = 0;
  EXPECT_EQ(read_class(bytes, options).error().kind, ReadErrorKind::ResourceLimitExceeded);
  EXPECT_EQ(upstream.live, 0u);
}

TEST(ClassOwnership, AllocationBudgetFailuresAcrossAttributeStorageAreRecoverable) {
  std::ifstream file(HIPPOCAFE_FIXTURE_DIR "/generated/ManyOpaqueAttributes.class", std::ios::binary);
  ASSERT_TRUE(file);
  const std::string data(std::istreambuf_iterator<char>{file}, {});
  const auto bytes = std::as_bytes(std::span(data));
  const auto parsed = read_class(bytes, STRICT);
  ASSERT_TRUE(parsed) << parsed.error().detail;

  // Exercise limits around the CP allocation and the retained attribute table.
  CountingResource upstream;
  ReadOptions options{.upstream_resource = &upstream};
  options.limits.max_total_allocated_bytes = 22289;
  const auto outcome = read_class(bytes, options);
  if (!outcome) {
    EXPECT_EQ(outcome.error().kind, ReadErrorKind::ResourceLimitExceeded);
    EXPECT_EQ(upstream.live, 0u);
  }
  CountingResource sweep;
  options.upstream_resource = &sweep;
  for (std::size_t limit = bytes.size(); limit < bytes.size() + 65536; limit += 257) {
    options.limits.max_total_allocated_bytes = limit;
    {
      const auto outcome = read_class(bytes, options);
      if (!outcome) {
        EXPECT_EQ(outcome.error().kind, ReadErrorKind::ResourceLimitExceeded) << limit;
      }
    }
    EXPECT_EQ(sweep.live, 0u) << limit;
  }
}

TEST(ClassReader, DropsInvalidNamesDuplicateAndMalformedBodiesWithBoundedDiagnostics) {
  auto model = fixture();
  const auto source = text(model, "SourceFile");
  const auto file_name = text(model, "Fixture.java");
  const auto unknown = text(model, "UnknownAttribute");
  const auto deprecated = text(model, "Deprecated");
  auto bytes = write_class(model, PRESERVE).value().bytes;
  bytes.resize(bytes.size() - 2);
  detail::BinaryWriter wire(bytes);
  wire.u2(5);
  const auto source_file = [&] {
    wire.u2(source.value);
    wire.u4(2);
    wire.u2(file_name.value);
  };
  source_file();
  wire.u2(0);
  wire.u4(0);
  source_file();
  wire.u2(deprecated.value);
  wire.u4(1);
  wire.u1(0xab);
  wire.u2(unknown.value);
  wire.u4(0);

  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::InvalidIndex);
  ReadOptions compatible;
  compatible.limits.max_diagnostics = 2;
  auto read = read_class(bytes, compatible);
  ASSERT_TRUE(read) << read.error().detail;
  ASSERT_EQ(read->diagnostics.size(), 3u);
  EXPECT_EQ(read->diagnostics[0].code, DiagnosticCode::InvalidAttributeName);
  EXPECT_EQ(read->diagnostics[1].code, DiagnosticCode::DuplicateAttribute);
  EXPECT_EQ(read->diagnostics[2].code, DiagnosticCode::DiagnosticsSuppressed);
  EXPECT_EQ(read->class_file.validity(), ClassValidity::ParsedRecovered);
  const auto wrote = write_class(read->class_file, PRESERVE).value();
  const auto reparsed = read_class(wrote.bytes, STRICT);
  ASSERT_TRUE(reparsed);
  const auto attributes = reparsed->class_file.attributes();
  ASSERT_EQ(attributes.size(), 2u);
  EXPECT_EQ(attributes[0].get<SourceFile>()->file_name, file_name);
  EXPECT_EQ(attributes[1].name_index(), unknown);
}

TEST(ClassReader, ReportsTrailingBytesAndWritesOnlyTheClassEnvelope) {
  const auto model = fixture();
  const auto golden = write_class(model).value().bytes;
  auto bytes = golden;
  bytes.push_back(std::byte{0xff});
  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::TrailingBytes);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible);
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics[0].offset, golden.size());
  EXPECT_EQ(write_class(compatible->class_file).value().bytes, golden);
}

TEST(ClassReader, DropsMalformedFieldConstantValueAndPreservesNeighbors) {
  auto model = fixture();
  const auto field = text(model, "field");
  const auto type = text(model, "I");
  const auto before = text(model, "VendorBefore");
  const auto value = text(model, "ConstantValue");
  const auto after = text(model, "VendorAfter");
  auto bytes = write_class(model, PRESERVE).value().bytes;
  bytes.resize(bytes.size() - 6);
  detail::BinaryWriter wire(bytes);
  wire.u2(1);
  wire.u2(0x0018); // static final
  wire.u2(field.value);
  wire.u2(type.value);
  wire.u2(3);
  wire.u2(before.value);
  wire.u4(1);
  wire.u1(0x47);
  wire.u2(value.value);
  wire.u4(2);
  wire.u2(type.value); // Utf8 where ConstantValue requires Integer
  wire.u2(after.value);
  wire.u4(0);
  wire.u2(0);
  wire.u2(0);

  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::MalformedAttribute);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible) << compatible.error().detail;
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  const auto repaired = write_class(compatible->class_file, PRESERVE).value();
  const auto reparsed = read_class(repaired.bytes, STRICT);
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  ASSERT_EQ(reparsed->class_file.fields().size(), 1u);
  const auto attributes = reparsed->class_file.fields()[0].attributes();
  ASSERT_EQ(attributes.size(), 2u);
  EXPECT_EQ(attributes[0].name_index(), before);
  EXPECT_EQ(attributes[1].name_index(), after);
  EXPECT_EQ(attributes[0].get<RawAttribute>()->bytes(reparsed->class_file).value().front(), std::byte{0x47});
}

TEST(ClassReader, DuplicateMembersAreComparedByUtf8Contents) {
  for (const bool method : {false, true}) {
    auto model = fixture();
    const auto name1 = text(model, "duplicate");
    const auto name2 = text(model, "duplicate");
    const auto type1 = text(model, method ? "()V" : "I");
    const auto type2 = text(model, method ? "()V" : "I");
    ASSERT_NE(name1, name2);
    ASSERT_NE(type1, type2);
    auto bytes = write_class(model, PRESERVE).value().bytes;
    bytes.resize(bytes.size() - 6);
    detail::BinaryWriter wire(bytes);
    if (method)
      wire.u2(0);
    wire.u2(2);
    constexpr std::uint16_t NATIVE_STATIC = 0x0109;
    constexpr std::uint16_t STATIC_FIELD = 0x0009;
    const auto write_member = [&](CpIndex name, CpIndex type) {
      wire.u2(method ? NATIVE_STATIC : STATIC_FIELD);
      wire.u2(name.value);
      wire.u2(type.value);
      wire.u2(0);
    };
    write_member(name1, type1);
    write_member(name2, type2);
    if (!method)
      wire.u2(0);
    wire.u2(0);
    const auto read = read_class(bytes);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().kind, ReadErrorKind::InvalidClass);
    EXPECT_EQ(read.error().path, method ? "methods[1]" : "fields[1]");

    if (method) {
      ASSERT_TRUE(model.add_method(NATIVE_STATIC, name1, type1));
      ASSERT_TRUE(model.add_method(NATIVE_STATIC, name2, type2));
    } else {
      ASSERT_TRUE(model.add_field(STATIC_FIELD, name1, type1));
      ASSERT_TRUE(model.add_field(STATIC_FIELD, name2, type2));
    }
    const auto written = write_class(model);
    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().kind, WriteErrorKind::InvalidClassModel);
  }
}

TEST(ClassReader, GatesClassDependentAttributesBeforeReadingTheirBodies) {
  auto model = ClassFile::create({61, 0}, "Final", "java/lang/Object", 0x0031).value();
  const auto permitted = text(model, "PermittedSubclasses");
  const auto original = write_class(model, PRESERVE).value().bytes;
  auto bytes = original;
  bytes.back() = std::byte{1}; // one class attribute
  detail::BinaryWriter wire(bytes);
  wire.u2(permitted.value);
  wire.u4(0); // illegal location even before decoding the malformed body
  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::InvalidAttributeContext);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible);
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics[0].code, DiagnosticCode::InvalidAttributeContext);
  EXPECT_EQ(write_class(compatible->class_file, PRESERVE).value().bytes, original);
  ASSERT_TRUE(model.add_attribute(Attribute::raw(permitted, {std::byte{0}, std::byte{0}})));
  EXPECT_EQ(write_class(model).error().kind, WriteErrorKind::InvalidClassModel);
}

TEST(ClassModel, ClassInitializerAndImplicitReceiverRespectVersionAndSlotBounds) {
  auto model = fixture();
  const auto clinit = text(model, "<clinit>");
  const auto none = text(model, "()V");
  const auto int_arg = text(model, "(I)V");
  const auto named = text(model, "nativeMethod");
  EXPECT_EQ(model.add_method(0, clinit, none).error(), ModelError::InvalidReference);
  EXPECT_EQ(model.add_method(0x0008, clinit, int_arg).error(), ModelError::InvalidReference);
  const auto full_desc = text(model, "(" + std::string(255, 'I') + ")V");
  EXPECT_EQ(model.add_method(0x0101, named, full_desc).error(), ModelError::InvalidReference);
  ASSERT_TRUE(model.add_method(0x0109, named, full_desc));

  auto legacy = fixture({50, 0});
  const auto member = legacy.add_method(0, text(legacy, "<clinit>"), text(legacy, "()V"));
  ASSERT_TRUE(member);
  auto& code = legacy.add_code(*member, text(legacy, "Code")).value().get();
  ASSERT_TRUE(code.code().append(Instruction::simple(Opcode::Return).value()));
  code.supply_metadata(0, 0, {});
  auto class_bytes = write_class(legacy, SUPPLIED).value().bytes;
  EXPECT_TRUE(read_class(class_bytes, STRICT));
  class_bytes[7] = std::byte{51};
  EXPECT_FALSE(read_class(class_bytes, STRICT));
}

TEST(ClassModel, FindsOverloadedMembersByExactNameAndDescriptor) {
  auto file = fixture();
  constexpr std::uint16_t NATIVE_STATIC = 0x0109;
  const auto field_name = text(file, "value");
  const auto duplicate_field_name = text(file, "value");
  const auto method_name = text(file, "run");
  const auto duplicate_method_name = text(file, "run");
  const auto field_int = file.add_field(0, field_name, text(file, "I")).value();
  const auto field_long = file.add_field(0, duplicate_field_name, text(file, "J")).value();
  const auto no_args = file.add_method(NATIVE_STATIC, method_name, text(file, "()V")).value();
  const auto int_arg = file.add_method(NATIVE_STATIC, duplicate_method_name, text(file, "(I)V")).value();

  const auto& view = std::as_const(file);
  EXPECT_EQ(view.find_field("value", "I"), field_int);
  EXPECT_EQ(view.find_field("value", "J"), field_long);
  EXPECT_EQ(view.find_field("value", "D"), std::nullopt);
  EXPECT_EQ(view.find_field("Value", "I"), std::nullopt);
  EXPECT_EQ(view.find_method("run", "()V"), no_args);
  EXPECT_EQ(view.find_method("run", "(I)V"), int_arg);
  EXPECT_EQ(view.find_method("run", "(I)I"), std::nullopt);
  EXPECT_EQ(view.find_method("Run", "()V"), std::nullopt);
}

TEST(ClassModel, ErasingMembersPreservesOrderAndSymbolicIndices) {
  auto model = fixture();
  const auto field_type = text(model, "I");
  const auto first = text(model, "first");
  const auto removed = text(model, "removed");
  const auto last = text(model, "last");
  ASSERT_TRUE(model.add_field(0x0001, first, field_type));
  ASSERT_TRUE(model.add_field(0x0001, removed, field_type));
  const auto vendor = text(model, "Vendor");
  std::vector<Attribute> attributes;
  attributes.push_back(Attribute::raw(vendor, {std::byte{0x34}}));
  ASSERT_TRUE(model.add_field(0x0001, last, field_type, std::move(attributes)));

  (void)with_return(model, "first");
  (void)with_return(model, "removed");
  (void)with_return(model, "last");
  const auto signature = model.constant_pool().append(
                                                  CpEntry{CpTag::NameAndType, 0, model.methods()[1].name_index(), model.methods()[1].descriptor_index()})
                             .value();
  const auto reference = model.constant_pool().append(CpEntry{CpTag::Methodref, 0, model.this_class(), signature}).value();
  const auto input = write_class(model, SUPPLIED_PRESERVE).value().bytes;
  auto parsed = read_class(input, STRICT);
  ASSERT_TRUE(parsed);
  auto& file = parsed->class_file;
  const auto pool_count = file.constant_pool().size();
  EXPECT_EQ(file.erase_method(3).error(), ModelError::InvalidMethod);
  EXPECT_EQ(file.erase_field(3).error(), ModelError::InvalidField);
  const auto field = file.find_field("removed", "I");
  const auto method = file.find_method("removed", "()V");
  ASSERT_EQ(field, 1u);
  ASSERT_EQ(method, 1u);
  ASSERT_TRUE(file.erase_field(*field));
  ASSERT_TRUE(file.erase_method(*method));
  EXPECT_EQ(file.find_field("removed", "I"), std::nullopt);
  EXPECT_EQ(file.find_method("removed", "()V"), std::nullopt);
  EXPECT_EQ(file.find_field("last", "I"), 1u);
  EXPECT_EQ(file.find_method("last", "()V"), 1u);

  const auto written = write_class(file, PRESERVE);
  ASSERT_TRUE(written) << written.error().detail;
  const auto roundtrip = read_class(written->bytes, STRICT);
  ASSERT_TRUE(roundtrip) << roundtrip.error().detail;
  const auto& fresh = roundtrip->class_file;
  ASSERT_EQ(fresh.fields().size(), 2u);
  EXPECT_EQ(fresh.fields()[0].name_index(), first);
  EXPECT_EQ(fresh.fields()[1].name_index(), last);
  ASSERT_EQ(fresh.fields()[1].attributes().size(), 1u);
  const auto* raw = fresh.fields()[1].attributes().front().get<RawAttribute>();
  ASSERT_NE(raw, nullptr);
  EXPECT_EQ(raw->bytes(fresh).value().front(), std::byte{0x34});

  ASSERT_EQ(fresh.methods().size(), 2u);
  EXPECT_EQ(fresh.constant_pool().get_utf8(fresh.methods()[0].name_index()), "first");
  EXPECT_EQ(fresh.constant_pool().get_utf8(fresh.methods()[1].name_index()), "last");
  EXPECT_NE(fresh.methods()[1].code(), nullptr);
  EXPECT_EQ(fresh.constant_pool().size(), pool_count);
  EXPECT_TRUE(fresh.constant_pool().has(reference, CpTag::Methodref));
  EXPECT_EQ(fresh.constant_pool().get(reference)->second, signature);
}

TEST(ClassModel, MethodEditorMutatesTheOwnedCode) {
  auto model = fixture();
  (void)with_return(model);
  const auto native = model.add_method(0x0109, text(model, "nativeMethod"), text(model, "()V")).value();
  const auto input = write_class(model, SUPPLIED).value().bytes;
  auto parsed = read_class(input, STRICT);
  ASSERT_TRUE(parsed);
  auto& file = parsed->class_file;
  EXPECT_FALSE(file.edit_method(2));
  ASSERT_TRUE(file.edit_method(native));
  EXPECT_EQ(file.edit_method(native)->code(), nullptr);
  const auto index = file.find_method("run", "()V");
  ASSERT_TRUE(index);
  const auto editor = file.edit_method(*index);
  ASSERT_TRUE(editor);
  EXPECT_EQ(&editor->method(), &file.methods()[0]);
  auto* body = editor->code();
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(body, file.edit_code(0));
  EXPECT_EQ(body, editor->method().code());
  auto& code = body->code();
  const std::array<InstructionBuilder, 2> patch{Instruction::nop(), Instruction::nop()};
  ASSERT_TRUE(code.insert_before(code.at(0), patch));
  EXPECT_EQ(write_class(file).error().kind, WriteErrorKind::StaleCodeMetadata);
  body->supply_metadata(0, 0, {});
  const auto output = write_class(file, SUPPLIED);
  ASSERT_TRUE(output) << output.error().detail;
  const auto fresh = read_class(output->bytes, STRICT);
  ASSERT_TRUE(fresh) << fresh.error().detail;
  ASSERT_NE(fresh->class_file.methods()[0].code(), nullptr);
  EXPECT_EQ(fresh->class_file.methods()[0].code()->code().size(), 3u);
}

TEST(CodeWriting, RequiresFreshExplicitMetadataAndRejectsOpaqueOffsetData) {
  auto model = fixture();
  (void)with_return(model);
  const auto custom = text(model, "VendorOffsets");
  const auto frames = text(model, "StackMapTable");
  const auto golden = write_class(model, SUPPLIED_PRESERVE).value().bytes;
  auto read = read_class(golden, STRICT);
  ASSERT_TRUE(read);
  auto& parsed = read->class_file;
  auto* attribute = parsed.edit_code(0);
  ASSERT_NE(attribute, nullptr);
  auto& code = attribute->code();
  const auto nop = Instruction::simple(Opcode::Nop).value();
  ASSERT_TRUE(code.insert_before(code.at(0), nop));
  EXPECT_EQ(write_class(parsed).error().kind, WriteErrorKind::StaleCodeMetadata);
  EXPECT_EQ(write_class(parsed, SUPPLIED).error().kind, WriteErrorKind::StaleCodeMetadata);

  std::vector<Attribute> unknown;
  unknown.push_back(Attribute::raw(custom, {std::byte{0}, std::byte{0}}));
  attribute->supply_metadata(0, 0, {}, std::move(unknown));
  EXPECT_EQ(write_class(parsed, SUPPLIED).error().kind, WriteErrorKind::UnsafeOpaqueAttribute);

  std::vector<Attribute> malformed;
  malformed.push_back(Attribute::raw(frames, {std::byte{0}, std::byte{1}, std::byte{2}}));
  attribute->supply_metadata(0, 0, {}, std::move(malformed));
  EXPECT_EQ(write_class(parsed, SUPPLIED).error().kind, WriteErrorKind::InvalidClassModel);

  attribute->supply_metadata(0, 0, {});
  const auto wrote = write_class(parsed, SUPPLIED);
  ASSERT_TRUE(wrote) << wrote.error().detail;
  const auto reparsed = read_class(wrote->bytes, STRICT);
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  const auto& fresh = reparsed->class_file.methods()[0].code()->code();
  ASSERT_EQ(fresh.size(), 2u);
  EXPECT_EQ(fresh.get(fresh.at(0)).value().opcode(), Opcode::Nop);
  EXPECT_EQ(fresh.get(fresh.at(1)).value().opcode(), Opcode::Return);
  EXPECT_EQ(write_class(reparsed->class_file, {.force_reencode = true}).value().bytes, wrote->bytes);

  ASSERT_TRUE(code.insert_before(code.at(0), nop));
  EXPECT_EQ(write_class(parsed, SUPPLIED).error().kind, WriteErrorKind::StaleCodeMetadata);
}

TEST(CodeWriting, CanonicalEncodingRequiresMetadataWhenBoundariesChange) {
  auto model = fixture();
  const auto name = text(model, "run");
  const auto descriptor = text(model, "(I)V");
  const auto code_name = text(model, "Code");
  auto bytes = write_class(model, PRESERVE).value().bytes;
  bytes.resize(bytes.size() - 4); // methods_count and attributes_count
  detail::BinaryWriter wire(bytes);
  wire.u2(1);
  wire.u2(9);
  wire.u2(name.value);
  wire.u2(descriptor.value);
  wire.u2(1);
  wire.u2(code_name.value);
  wire.u4(16);
  wire.u2(1);
  wire.u2(1);
  wire.u4(4);
  wire.u1(0x15); // iload 0 in its longer explicit encoding
  wire.u1(0);
  wire.u1(0x57);
  wire.u1(0xb1);
  wire.u2(0);
  wire.u2(0);
  wire.u2(0);

  auto read = read_class(bytes, STRICT);
  ASSERT_TRUE(read) << read.error().detail;
  auto& parsed = read->class_file;
  EXPECT_EQ(write_class(parsed, {.force_reencode = true, .constant_pool = ConstantPoolWritePolicy::Preserve}).value().bytes, bytes);
  EXPECT_EQ(write_class(parsed, {.preserve_original_encodings = false}).error().kind, WriteErrorKind::StaleCodeMetadata);
  const WriteOptions canonical{.code_policy = CodeWritePolicy::CallerSuppliedMetadata,
                               .preserve_original_encodings = false};
  parsed.edit_code(0)->supply_metadata(1, 1, {});
  EXPECT_EQ(write_class(parsed, canonical).error().kind, WriteErrorKind::StaleCodeMetadata);
  parsed.edit_code(0)->supply_metadata(1, 1, {}, {}, false);
  const auto wrote = write_class(parsed, canonical);
  ASSERT_TRUE(wrote) << wrote.error().detail;
  EXPECT_EQ(wrote->bytes.size(), bytes.size() - 1);
  EXPECT_TRUE(read_class(wrote->bytes, STRICT));

  // The same method now fits only if its saved encoding hints are relaxed.
  // Selecting a different layout must be explicit, independent of whether
  // any supplied raw offsets would accidentally still be in range.
  auto& method = *parsed.edit_code(0);
  auto& code = method.code();
  code.reserve(MAX_CODE_SIZE);
  const auto nop = Instruction::simple(Opcode::Nop).value();
  for (std::size_t i = 0; i < MAX_CODE_SIZE - 3; ++i) {
    ASSERT_TRUE(code.append(nop));
  }
  method.supply_metadata(1, 1, {});
  const auto too_large = write_class(parsed, SUPPLIED);
  ASSERT_FALSE(too_large);
  EXPECT_EQ(too_large.error().kind, WriteErrorKind::CodeTooLarge);
}

} // namespace
} // namespace cafe
