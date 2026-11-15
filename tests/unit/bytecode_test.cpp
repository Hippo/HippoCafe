#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <random>
#include <ranges>
#include <vector>

#include <gtest/gtest.h>

#include "cafe/bytecode/code.hpp"
#include "cafe/constant_pool.hpp"

#include "io/binary_writer.hpp"
#include "io/code_decoder.hpp"
#include "io/code_encoder.hpp"
#include "io/code_operands.hpp"

namespace cafe {
namespace {

using detail::BinaryWriter;
using detail::decode_code;
using detail::encode_code;

constexpr ClassVersion LEGACY{50, 0};
constexpr ClassVersion MODERN{65, 0};
constexpr std::uint16_t MAX_LOCALS = 512;
constexpr std::string_view PATH = "methods[0].Code";

struct Operands {
  ConstantPool pool;
  CpIndex integer = pool.append(CpEntry{CpTag::Integer, 0, {}, {}}).value();
  CpIndex long_value = pool.append(CpEntry{CpTag::Long, 0, {}, {}}).value();
  CpIndex class_name = pool.append_utf8("BytecodeTest").value();
  CpIndex class_value = pool.append(CpEntry{CpTag::Class, 0, class_name, {}}).value();
  CpIndex array_name = pool.append_utf8("[[I").value();
  CpIndex array_value = pool.append(CpEntry{CpTag::Class, 0, array_name, {}}).value();
  CpIndex member_name = pool.append_utf8("member").value();
  CpIndex field_descriptor = pool.append_utf8("J").value();
  CpIndex method_descriptor = pool.append_utf8("(J[DD)V").value();
  CpIndex field_signature = pool.append(CpEntry{CpTag::NameAndType, 0, member_name, field_descriptor}).value();
  CpIndex method_signature = pool.append(CpEntry{CpTag::NameAndType, 0, member_name, method_descriptor}).value();
  CpIndex field = pool.append(CpEntry{CpTag::Fieldref, 0, class_value, field_signature}).value();
  CpIndex method = pool.append(CpEntry{CpTag::Methodref, 0, class_value, method_signature}).value();
  CpIndex interface_method = pool.append(CpEntry{CpTag::InterfaceMethodref, 0, class_value, method_signature}).value();
  CpIndex invokedynamic = pool.append(CpEntry{CpTag::InvokeDynamic, 0, {}, method_signature}).value();
  CpIndex dynamic = pool.append(CpEntry{CpTag::Dynamic, 0, {}, field_signature}).value();
};

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
  std::vector<std::byte> data;
  for (auto value : values)
    data.push_back(static_cast<std::byte>(value));
  return data;
}

std::vector<std::byte> example(Opcode opcode, const Operands& operands, unsigned prefix = 0) {
  std::vector<std::byte> data(prefix, std::byte{0});
  BinaryWriter writer(data);
  auto start = static_cast<std::uint32_t>(data.size());
  writer.u1(static_cast<std::uint8_t>(opcode));
  const auto& info = opcode_info(opcode);
  switch (info.form) {
    case OperandForm::SignedByte:
      writer.s1(INT8_MIN);
      break;
    case OperandForm::SignedShort:
      writer.s2(INT16_MIN);
      break;
    case OperandForm::Local:
      writer.u1(3);
      break;
    case OperandForm::Increment:
      writer.u1(255);
      writer.s1(INT8_MIN);
      break;
    case OperandForm::ConstantByte:
      writer.u1(static_cast<std::uint8_t>(operands.integer.value));
      break;
    case OperandForm::ConstantShort: {
      auto index = operands.class_value;
      if (opcode == Opcode::LdcW)
        index = operands.integer;
      else if (opcode == Opcode::Ldc2W)
        index = operands.long_value;
      else if (opcode >= Opcode::GetStatic && opcode <= Opcode::PutField)
        index = operands.field;
      else if (opcode >= Opcode::InvokeVirtual && opcode <= Opcode::InvokeStatic)
        index = operands.method;
      writer.u2(index.value);
      break;
    }
    case OperandForm::BranchShort:
      writer.s2(static_cast<std::int16_t>(-static_cast<std::int32_t>(start)));
      break;
    case OperandForm::BranchInt:
      writer.s4(-static_cast<std::int32_t>(start));
      break;
    case OperandForm::TableSwitch:
    case OperandForm::LookupSwitch: {
      for (auto padding = detail::switch_padding(start); padding != 0; --padding)
        writer.u1(0);
      // Default targets the return after the switch. Negative keys target
      // the method entry. For start == 0 the switch targets itself.
      bool table = info.form == OperandForm::TableSwitch;
      writer.s4(static_cast<std::int32_t>(detail::switch_padding(start)) + (table ? 21 : 25));
      if (table) {
        writer.s4(-2);
        writer.s4(-1);
      } else {
        writer.s4(2);
        writer.s4(-2);
      }
      writer.s4(-static_cast<std::int32_t>(start));
      if (!table)
        writer.s4(42);
      writer.s4(-static_cast<std::int32_t>(start));
      break;
    }
    case OperandForm::InvokeInterface:
      writer.u2(operands.interface_method.value);
      writer.u1(6);
      writer.u1(0);
      break;
    case OperandForm::InvokeDynamic:
      writer.u2(operands.invokedynamic.value);
      writer.u2(0);
      break;
    case OperandForm::NewArray:
      writer.u1(static_cast<std::uint8_t>(ArrayType::Boolean));
      break;
    case OperandForm::MultiANewArray:
      writer.u2(operands.array_value.value);
      writer.u1(2);
      break;
    case OperandForm::WidePrefix:
      writer.u1(static_cast<std::uint8_t>(Opcode::IInc));
      writer.u2(400);
      writer.s2(INT16_MIN);
      break;
    default:
      break;
  }
  writer.u1(static_cast<std::uint8_t>(Opcode::Return));
  return data;
}

std::size_t target_position(const MethodCode& code, LabelId target) {
  auto id = code.label_position(target).value().before.value();
  for (std::size_t i = 0; i < code.size(); ++i) {
    if (code.at(i) == id)
      return i;
  }
  return code.size();
}

void compare_instructions(const MethodCode& first, const MethodCode& second) {
  ASSERT_EQ(first.size(), second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    auto first_id = first.at(i);
    auto second_id = second.at(i);
    auto left = first.get(first_id).value();
    auto right = second.get(second_id).value();
    EXPECT_EQ(left.opcode(), right.opcode());
    EXPECT_EQ(left.local_index(), right.local_index());
    EXPECT_EQ(left.immediate_value(), right.immediate_value());
    EXPECT_EQ(left.constant_index(), right.constant_index());
    EXPECT_EQ(left.interface_count(), right.interface_count());
    EXPECT_EQ(left.dimensions(), right.dimensions());
    EXPECT_EQ(left.array_type(), right.array_type());
    if (auto increment = left.increment_value()) {
      ASSERT_TRUE(right.increment_value());
      EXPECT_EQ(increment->amount, right.increment_value()->amount);
      EXPECT_EQ(increment->local, right.increment_value()->local);
    }
    if (auto branch = first.branch_target(first_id)) {
      EXPECT_EQ(target_position(first, *branch), target_position(second, second.branch_target(second_id).value()));
    }
    if (left.opcode() == Opcode::TableSwitch) {
      auto a = first.table_switch(first.switch_id(first_id).value()).value();
      auto b = second.table_switch(second.switch_id(second_id).value()).value();
      EXPECT_EQ(a.low, b.low);
      EXPECT_EQ(target_position(first, a.default_target), target_position(second, b.default_target));
      ASSERT_EQ(a.targets.size(), b.targets.size());
      for (std::size_t j = 0; j < a.targets.size(); ++j)
        EXPECT_EQ(target_position(first, a.targets[j]), target_position(second, b.targets[j]));
    }
    if (left.opcode() == Opcode::LookupSwitch) {
      auto a = first.lookup_switch(first.switch_id(first_id).value()).value();
      auto b = second.lookup_switch(second.switch_id(second_id).value()).value();
      EXPECT_EQ(target_position(first, a.default_target), target_position(second, b.default_target));
      ASSERT_EQ(a.pairs.size(), b.pairs.size());
      for (std::size_t j = 0; j < a.pairs.size(); ++j) {
        EXPECT_EQ(a.pairs[j].key, b.pairs[j].key);
        EXPECT_EQ(target_position(first, a.pairs[j].target), target_position(second, b.pairs[j].target));
      }
    }
  }
}

void verify_encodings(std::span<const std::byte> original, const ConstantPool& pool, ClassVersion version) {
  std::pmr::monotonic_buffer_resource scratch;
  MethodCode decoded;
  auto boundaries = decode_code(decoded, original, 20, version, pool, MAX_LOCALS, {}, &scratch, PATH);
  ASSERT_TRUE(boundaries) << boundaries.error().detail;
  EXPECT_FALSE(decoded.edited());
  auto preserved = encode_code(decoded, version, pool, MAX_LOCALS, true, &scratch, PATH);
  ASSERT_TRUE(preserved) << preserved.error().detail;
  EXPECT_TRUE(std::ranges::equal(preserved->bytes(), original));
  EXPECT_TRUE(std::ranges::equal(preserved->instruction_starts(), boundaries->instruction_starts()));
  EXPECT_EQ(preserved->instruction_offsets().back(), original.size());

  auto canonical = encode_code(decoded, version, pool, MAX_LOCALS, false, &scratch, PATH);
  ASSERT_TRUE(canonical) << canonical.error().detail;
  MethodCode verified;
  auto reparsed = decode_code(verified, canonical->bytes(), 0, version, pool, MAX_LOCALS, {}, &scratch, PATH);
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  compare_instructions(decoded, verified);
}

TEST(Bytecode, DecodesAndFreshlyEncodesEveryWireOpcode) {
  Operands operands;
  for (unsigned value = 0; value <= 0xc9; ++value) {
    SCOPED_TRACE(value);
    auto opcode = static_cast<Opcode>(value);
    const auto version = opcode == Opcode::InvokeDynamic ? MODERN : LEGACY;
    ASSERT_NE(opcode_info(opcode).form, OperandForm::Illegal);
    auto original = example(opcode, operands);
    verify_encodings(original, operands.pool, version);
    if (opcode_info(opcode).form != OperandForm::None && opcode_info(opcode).form != OperandForm::ImplicitLocal) {
      // Remove the return and the final operand byte.
      original.resize(original.size() - 2);
      std::pmr::monotonic_buffer_resource scratch;
      MethodCode decoded;
      EXPECT_FALSE(decode_code(decoded, original, 0, version, operands.pool, MAX_LOCALS, {}, &scratch, PATH));
    }
  }
  for (unsigned value = 0xca; value <= 0xff; ++value) {
    SCOPED_TRACE(value);
    EXPECT_EQ(opcode_info(static_cast<Opcode>(value)).form, OperandForm::Illegal);
    std::pmr::monotonic_buffer_resource scratch;
    MethodCode decoded;
    auto invalid = decode_code(decoded, bytes({value}), 0, MODERN, operands.pool, MAX_LOCALS, {}, &scratch, PATH);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().kind, ReadErrorKind::InvalidOpcode);
  }
}

TEST(Bytecode, EncodesAllWideFamiliesAndSwitchAlignments) {
  Operands operands;
  for (auto opcode : {Opcode::ILoad, Opcode::LLoad, Opcode::FLoad, Opcode::DLoad, Opcode::ALoad,
                      Opcode::IStore, Opcode::LStore, Opcode::FStore, Opcode::DStore, Opcode::AStore, Opcode::Ret}) {
    SCOPED_TRACE(static_cast<unsigned>(opcode));
    verify_encodings(bytes({0xc4, static_cast<unsigned>(opcode), 1, 24, 0xb1}), operands.pool, LEGACY);
  }
  for (auto opcode : {Opcode::TableSwitch, Opcode::LookupSwitch}) {
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
      SCOPED_TRACE(alignment);
      verify_encodings(example(opcode, operands, alignment), operands.pool, MODERN);
    }
  }

  // Empty lookup is legal, as is an INT32_MIN key in a table.
  verify_encodings(bytes({0xab, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0xb1}), operands.pool, MODERN);
  verify_encodings(bytes({0xaa, 0, 0, 0, 0, 0, 0, 20, 0x80, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 20, 0xb1}),
                   operands.pool, MODERN);
}

TEST(Bytecode, ChecksOperandStructureAndVersionBeforeCommit) {
  Operands operands;
  std::pmr::monotonic_buffer_resource scratch;
  auto expect_error = [&](std::span<const std::byte> wire, ReadErrorKind kind, ClassVersion version = MODERN,
                          ReadOptions options = {}) {
    MethodCode decoded;
    auto result = decode_code(decoded, wire, 51, version, operands.pool, MAX_LOCALS, options, &scratch, PATH);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, kind);
    EXPECT_EQ(result.error().offset, 51u);
    EXPECT_EQ(result.error().path, PATH);
    scratch.release();
  };
  expect_error(bytes({0xa7, 0, 1, 0xb1}), ReadErrorKind::InvalidBranchTarget);
  expect_error(bytes({0xa7, 0, 4, 0xb1}), ReadErrorKind::InvalidBranchTarget);
  expect_error(bytes({0xa7, 0xff, 0xff, 0xb1}), ReadErrorKind::InvalidBranchTarget);
  expect_error(bytes({0xc4, 0, 0, 1}), ReadErrorKind::InvalidOpcode);
  expect_error(bytes({0xc4, 0x16, 1, 0xff}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xc4, 0xa9, 0, 5}), ReadErrorKind::InvalidOpcode);
  expect_error(bytes({0xa8, 0, 0}), ReadErrorKind::InvalidOpcode);
  expect_error(bytes({0xbc, 12}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xc5, 0, operands.array_value.value, 3}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xb9, 0, operands.interface_method.value, 6, 1}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xb9, 0, operands.interface_method.value, 5, 0}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xba, 0, operands.invokedynamic.value, 1, 0}), ReadErrorKind::InvalidInstruction);
  expect_error(bytes({0xba, 0, operands.invokedynamic.value, 0, 0}), ReadErrorKind::InvalidOpcode, LEGACY);
  expect_error(bytes({0x12, 0}), ReadErrorKind::InvalidIndex);
  expect_error(bytes({0x12, operands.long_value.value}), ReadErrorKind::InvalidIndex);
  expect_error(bytes({0x14, 0, operands.integer.value}), ReadErrorKind::InvalidIndex);
  expect_error(bytes({0x12, operands.dynamic.value}), ReadErrorKind::InvalidIndex);
  expect_error(bytes({0x12, operands.class_value.value}), ReadErrorKind::InvalidIndex, ClassVersion{48, 0});
  verify_encodings(bytes({0x14, 0, operands.dynamic.value, 0xb1}), operands.pool, MODERN);

  auto table = example(Opcode::TableSwitch, operands);
  table[1] = std::byte{1};
  expect_error(table, ReadErrorKind::InvalidInstruction, LEGACY);
  verify_encodings(table, operands.pool, MODERN);
  table[1] = std::byte{0};
  table[7] = std::byte{1};
  expect_error(table, ReadErrorKind::InvalidBranchTarget);
  table[7] = std::byte{24};
  ReadOptions limited;
  limited.limits.max_switch_cases = 1;
  expect_error(table, ReadErrorKind::ResourceLimitExceeded, MODERN, limited);
  table[12] = std::byte{0x7f};
  expect_error(table, ReadErrorKind::TruncatedInput);

  auto lookup = example(Opcode::LookupSwitch, operands);
  lookup[20] = std::byte{0xff};
  expect_error(lookup, ReadErrorKind::InvalidInstruction);
}

TEST(MethodCode, ScopeSafeHandlesAndExplicitLabelMovement) {
  MethodCode code;
  MethodCode other;
  const auto first = code.append(Instruction{}).value();
  const auto second = code.append(Instruction::simple(Opcode::Return).value()).value();
  const auto stranger = other.append(Instruction{}).value();
  EXPECT_FALSE(code.get(stranger));
  EXPECT_FALSE(code.get(code.at(900)));

  auto label = code.make_label().value();
  EXPECT_EQ(code.label_position(label).error(), CodeEditError::UnboundLabel);
  ASSERT_TRUE(code.bind_label(label, CodePosition{second}));
  const auto branch = code.insert_before(first, Branch{Opcode::Goto, label}).value();
  EXPECT_EQ(code.branch_target(branch).value(), label);
  EXPECT_EQ(other.append(Branch{Opcode::Goto, label}).error(), CodeEditError::InvalidLabel);
  EXPECT_FALSE(other.append(code.get(branch).value()));

  const auto untouched = code.insert_before(second, Instruction{}).value();
  EXPECT_EQ(code.label_position(label).value(), CodePosition{second});
  auto landed = code.insert_at(label, Instruction{}).value();
  EXPECT_EQ(code.label_position(label).value(), CodePosition{landed});
  EXPECT_EQ(code.erase(landed).error(), CodeEditError::LabeledInstruction);
  ASSERT_TRUE(code.bind_label(label, CodePosition{second}));
  ASSERT_TRUE(code.erase(landed));
  auto recycled = code.insert_before(second, Instruction{}).value();
  EXPECT_EQ(recycled.index, landed.index);
  EXPECT_NE(recycled.generation, landed.generation);
  EXPECT_FALSE(code.get(landed));
  EXPECT_FALSE(code.replace(landed, Instruction{}));
  EXPECT_EQ(code.at(2), untouched);

  auto revision = code.revision();
  code.reserve(512);
  EXPECT_EQ(code.revision(), revision);
  ASSERT_TRUE(code.move_before(second, CodePosition{first}));
  EXPECT_EQ(code.at(1), second);
  EXPECT_GT(code.revision(), revision);
  EXPECT_EQ(code.label_position(label).value(), CodePosition{second});
}

static_assert(std::ranges::forward_range<MethodCode::InstructionRange>);
static_assert(std::ranges::sized_range<MethodCode::InstructionRange>);
static_assert(std::ranges::borrowed_range<MethodCode::InstructionRange>);

TEST(MethodCode, InsertsBlockInOrderAndKeepsLabelsOnTheAnchor) {
  MethodCode code;
  EXPECT_TRUE(code.instructions().empty());
  const auto erased = code.append(Instruction::nop()).value();
  const auto anchor = code.append(Instruction::return_void()).value();
  const auto target = code.make_label().value();
  ASSERT_TRUE(code.bind_label(target, CodePosition{anchor}));
  ASSERT_TRUE(code.erase(erased));

  std::array cases{target};
  const std::array<InstructionBuilder, 3> patch{
      Instruction::bipush(-1), TableSwitch{-1, target, cases}, Branch{Opcode::Goto, target}};
  const auto revision = code.revision();
  const auto handles = code.insert_before(anchor, patch);
  ASSERT_TRUE(handles);
  ASSERT_EQ(handles->size(), patch.size());
  EXPECT_EQ(code.revision(), revision + 1);
  EXPECT_EQ(handles->front().index, erased.index);
  EXPECT_NE(handles->front().generation, erased.generation);
  EXPECT_FALSE(code.get(erased));
  EXPECT_EQ(code.label_position(target).value().before, anchor);
  EXPECT_EQ(code.branch_target(handles->back()).value(), target);

  cases[0] = {};
  const auto table = code.table_switch(code.switch_id((*handles)[1]).value()).value();
  ASSERT_EQ(table.targets.size(), 1u);
  EXPECT_EQ(table.targets.front(), target);

  const std::array expected{Opcode::Bipush, Opcode::TableSwitch, Opcode::Goto, Opcode::Return};
  ASSERT_EQ(code.instructions().size(), expected.size());
  std::size_t position = 0;
  for (const auto [id, instruction] : std::as_const(code).instructions()) {
    EXPECT_EQ(instruction.opcode(), expected[position]);
    EXPECT_EQ(id, position < handles->size() ? (*handles)[position] : anchor);
    ++position;
  }
  const auto found = std::ranges::find_if(code.instructions(), [](InstructionEntry entry) {
    return entry.instruction.opcode() == Opcode::Return;
  });
  EXPECT_EQ((*found).id, anchor);
  EXPECT_EQ(code.revision(), revision + 1);

  const auto empty = code.insert_before(anchor, std::span<const InstructionBuilder>{});
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
  EXPECT_EQ(code.revision(), revision + 1);
  const auto invalid = code.insert_before(erased, patch);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().cause, CodeEditError::InvalidInstruction);
  EXPECT_EQ(invalid.error().instruction_index, std::nullopt);
  // A braced empty single-instruction operand retains its prior meaning.
  EXPECT_TRUE(code.insert_before(anchor, {}));
}

TEST(MethodCode, FailedBlockLeavesExistingSwitchesAndHandlesIntact) {
  MethodCode code;
  const auto anchor = code.append(Instruction::return_void()).value();
  const auto target = code.make_label().value();
  ASSERT_TRUE(code.bind_label(target, CodePosition{anchor}));
  const std::array cases{target};
  const auto original = code.insert_before(anchor, TableSwitch{0, target, cases}).value();
  const auto erased = code.insert_before(anchor, LookupSwitch{target, {}}).value();
  ASSERT_TRUE(code.erase(erased));
  const auto existing = code.switch_id(original).value();
  const auto table = code.table_switch(existing).value();
  const std::array<InstructionBuilder, 3> bad{table, LookupSwitch{target, {}}, Branch{Opcode::Goto, {}}};
  const auto revision = code.revision();
  const auto rejected = code.insert_before(anchor, bad);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().cause, CodeEditError::InvalidLabel);
  EXPECT_EQ(rejected.error().instruction_index, 2u);
  EXPECT_EQ(code.revision(), revision);
  EXPECT_EQ(code.size(), 2u);
  EXPECT_EQ(code.at(0), original);
  EXPECT_EQ(code.at(1), anchor);
  EXPECT_FALSE(code.get(erased));
  EXPECT_EQ(code.label_position(target).value().before, anchor);
  EXPECT_EQ(code.table_switch(existing).value().targets.front(), target);

  const auto inserted = code.insert_before(anchor, std::span(bad).first(2));
  ASSERT_TRUE(inserted);
  ASSERT_EQ(inserted->size(), 2u);
  EXPECT_EQ(inserted->front().index, erased.index);
  EXPECT_NE(inserted->front().generation, erased.generation);
  EXPECT_TRUE(code.lookup_switch(code.switch_id(inserted->back()).value()));
  EXPECT_EQ(code.table_switch(existing).value().targets.front(), target);
}

TEST(MethodCode, AppendsBlocksAtomicallyInInputOrder) {
  MethodCode code;
  const std::array<InstructionBuilder, 2> bad{Instruction::nop(), Branch{Opcode::Goto, {}}};
  const auto failed = code.append(bad);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().cause, CodeEditError::InvalidLabel);
  EXPECT_EQ(failed.error().instruction_index, 1u);
  EXPECT_EQ(code.size(), 0u);
  EXPECT_EQ(code.revision(), 0u);

  const std::array<InstructionBuilder, 2> patch{Instruction::nop(), Instruction::return_void()};
  const auto inserted = code.append(patch);
  ASSERT_TRUE(inserted);
  ASSERT_EQ(inserted->size(), 2u);
  EXPECT_EQ(code.at(0), (*inserted)[0]);
  EXPECT_EQ(code.at(1), (*inserted)[1]);
  EXPECT_EQ(code.revision(), 1u);
  const auto empty = code.append(std::span<const InstructionBuilder>{});
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
  EXPECT_EQ(code.revision(), 1u);
  EXPECT_TRUE(code.append({}));
}

TEST(MethodCode, InsertsBlocksAtLabelsAndBindsOnlyTheSelectedLabel) {
  MethodCode code;
  const auto anchor = code.append(Instruction::return_void()).value();
  const auto target = code.make_label().value();
  const auto other = code.make_label().value();
  ASSERT_TRUE(code.bind_label(target, CodePosition{anchor}));
  ASSERT_TRUE(code.bind_label(other, CodePosition{anchor}));
  const auto revision = code.revision();

  const std::array<InstructionBuilder, 2> bad{Instruction::nop(), Branch{Opcode::Goto, {}}};
  const auto failed = code.insert_at(target, bad);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().cause, CodeEditError::InvalidLabel);
  EXPECT_EQ(failed.error().instruction_index, 1u);
  EXPECT_EQ(code.size(), 1u);
  EXPECT_EQ(code.revision(), revision);
  EXPECT_EQ(code.label_position(target).value().before, anchor);

  const std::array<InstructionBuilder, 2> patch{Instruction::nop(), Branch{Opcode::Goto, other}};
  const auto inserted = code.insert_at(target, patch);
  ASSERT_TRUE(inserted);
  ASSERT_EQ(inserted->size(), 2u);
  EXPECT_EQ(code.revision(), revision + 1);
  EXPECT_EQ(code.label_position(target).value().before, inserted->front());
  EXPECT_EQ(code.label_position(other).value().before, anchor);
  EXPECT_EQ(code.branch_target(inserted->back()).value(), other);

  const auto end = code.make_label().value();
  ASSERT_TRUE(code.bind_label(end, {}));
  const auto before_empty = code.revision();
  ASSERT_TRUE(code.insert_at(end, std::span<const InstructionBuilder>{}));
  EXPECT_FALSE(code.label_position(end).value().before);
  EXPECT_EQ(code.revision(), before_empty);
  const auto appended = code.insert_at(end, patch);
  ASSERT_TRUE(appended);
  EXPECT_EQ(code.label_position(end).value().before, appended->front());
  EXPECT_EQ(code.at(code.size() - 1), appended->back());

  const auto unbound = code.make_label().value();
  const auto invalid_anchor = code.insert_at(unbound, patch);
  ASSERT_FALSE(invalid_anchor);
  EXPECT_EQ(invalid_anchor.error().cause, CodeEditError::UnboundLabel);
  EXPECT_FALSE(invalid_anchor.error().instruction_index);
}

TEST(Bytecode, TypedBlockBuildersProduceWideEncodings) {
  MethodCode code;
  const auto anchor = code.append(Instruction::return_void()).value();
  const std::array<InstructionBuilder, 5> patch{
      Instruction::bipush(-128), Instruction::istore(300), Instruction::increment(300, 1000),
      Instruction::iload(300), Instruction::pop()};
  ASSERT_TRUE(code.insert_before(anchor, patch));
  std::pmr::monotonic_buffer_resource scratch;
  const Operands operands;
  const auto encoded = encode_code(code, MODERN, operands.pool, MAX_LOCALS, false, &scratch, PATH);
  ASSERT_TRUE(encoded);
  const auto expected = bytes({0x10, 0x80, // bipush -128
                               0xc4, 0x36, 0x01, 0x2c, // wide istore 300
                               0xc4, 0x84, 0x01, 0x2c, 3, 232, // wide iinc 300, 1000
                               0xc4, 0x15, 0x01, 0x2c, // wide iload 300
                               0x57, 0xb1});
  EXPECT_TRUE(std::ranges::equal(encoded->bytes(), expected));
  EXPECT_EQ(Instruction::sipush(-32768).immediate_value(), -32768);

  const std::array locals{
      Instruction::iload(300), Instruction::lload(300), Instruction::fload(300), Instruction::dload(300), Instruction::aload(300),
      Instruction::istore(300), Instruction::lstore(300), Instruction::fstore(300), Instruction::dstore(300), Instruction::astore(300)};
  const std::array opcodes{
      Opcode::ILoad, Opcode::LLoad, Opcode::FLoad, Opcode::DLoad, Opcode::ALoad,
      Opcode::IStore, Opcode::LStore, Opcode::FStore, Opcode::DStore, Opcode::AStore};
  for (std::size_t i = 0; i < locals.size(); ++i)
    EXPECT_EQ(locals[i], Instruction::local(opcodes[i], 300).value());
}

TEST(MethodCode, RandomEditsPreserveOrderAndRejectStaleIds) {
  MethodCode code;
  std::vector<InstructionId> order;
  std::vector<InstructionId> stale;
  std::mt19937 random(3571);
  for (unsigned step = 0; step < 1000; ++step) {
    if (order.empty() || random() % 2 == 0) {
      auto offset = random() % (order.size() + 1);
      auto id = offset == order.size() ? code.append(Instruction{}) : code.insert_before(order[offset], Instruction{});
      ASSERT_TRUE(id.has_value());
      order.insert(order.begin() + static_cast<std::ptrdiff_t>(offset), *id);
    } else {
      auto offset = random() % order.size();
      stale.push_back(order[offset]);
      ASSERT_TRUE(code.erase(order[offset]));
      order.erase(order.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    ASSERT_EQ(code.size(), order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      EXPECT_EQ(code.at(i), order[i]);
      EXPECT_TRUE(code.get(order[i]));
    }
    if (!stale.empty()) {
      EXPECT_FALSE(code.get(stale[random() % stale.size()]));
    }
  }
}

class AllocationCounter : public std::pmr::memory_resource {
public:
  std::size_t calls = 0;
  std::size_t live_bytes = 0;
  std::size_t peak_bytes = 0;
  std::optional<std::size_t> allocations_left;

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (allocations_left) {
      if (*allocations_left == 0)
        throw std::bad_alloc();
      --*allocations_left;
    }
    void* allocated = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    ++calls;
    live_bytes += bytes;
    peak_bytes = std::max(peak_bytes, live_bytes);
    return allocated;
  }
  void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override {
    live_bytes -= bytes;
    std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

TEST(MethodCode, AllocationFailureDuringBlockPreparationLeavesCodeIntact) {
  bool succeeded = false;
  for (std::size_t budget = 0; budget < 16 && !succeeded; ++budget) {
    SCOPED_TRACE(budget);
    AllocationCounter resource;
    {
      MethodCode code(&resource);
      const auto anchor = code.append(Instruction::return_void()).value();
      const auto target = code.make_label().value();
      ASSERT_TRUE(code.bind_label(target, CodePosition{anchor}));
      const std::array cases{target};
      const std::array pairs{LookupPair{7, target}};
      const std::array<InstructionBuilder, 3> patch{
          TableSwitch{0, target, cases}, LookupSwitch{target, pairs}, Instruction::nop()};
      const auto revision = code.revision();
      resource.allocations_left = budget;
      try {
        ASSERT_TRUE(code.insert_before(anchor, patch));
        succeeded = true;
      } catch (const std::bad_alloc&) {
        EXPECT_EQ(code.revision(), revision);
        EXPECT_EQ(code.size(), 1u);
        EXPECT_EQ((*code.instructions().begin()).id, anchor);
        EXPECT_EQ(code.label_position(target).value().before, anchor);
      }
    }
    EXPECT_EQ(resource.live_bytes, 0u);
  }
  EXPECT_TRUE(succeeded);
}

TEST(MethodCode, NoPerInstructionAllocationAndReclaimsSwitchesAfterReplace) {
  AllocationCounter resource;
  {
    MethodCode code(&resource);
    code.reserve(2048);
    auto allocations = resource.calls;
    for (unsigned i = 0; i < 2000; ++i)
      ASSERT_TRUE(code.append(Instruction{}));
    EXPECT_EQ(resource.calls, allocations);

    auto target = code.make_label().value();
    ASSERT_TRUE(code.bind_label(target, CodePosition{code.at(0)}));
    std::array<LabelId, 64> targets;
    targets.fill(target);
    auto id = code.append(TableSwitch{-64, target, targets}).value();
    auto switch_id = code.switch_id(id).value();
    EXPECT_EQ(code.table_switch(switch_id)->targets.size(), targets.size());
    auto live = resource.live_bytes;
    ASSERT_TRUE(code.replace(id, Instruction{}));
    EXPECT_LT(resource.live_bytes, live);
    ASSERT_FALSE(code.table_switch(switch_id));
    ASSERT_TRUE(code.replace(id, LookupSwitch{target, {}}));
    auto reused = code.switch_id(id).value();
    EXPECT_NE(reused, switch_id);
    EXPECT_TRUE(code.lookup_switch(reused));
    EXPECT_FALSE(code.lookup_switch(switch_id));
    ASSERT_TRUE(code.erase(id));
    EXPECT_FALSE(code.lookup_switch(reused));
  }
  EXPECT_EQ(resource.live_bytes, 0u);
}

TEST(Bytecode, BranchLimitsAndCodeSizeAreCheckedAfterLayout) {
  Operands operands;
  auto branch_example = [&](Opcode opcode, std::uint32_t displacement, bool backwards) {
    MethodCode code;
    code.reserve(displacement + 2);
    auto target = code.make_label().value();
    if (!backwards) {
      EXPECT_TRUE(code.append(Branch{opcode, target}));
    }
    auto nop_count = backwards ? displacement : displacement - 3;
    for (std::uint32_t i = 0; i < nop_count; ++i)
      code.append(Instruction{}).value();
    auto terminal = code.append(backwards ? InstructionBuilder(Branch{opcode, target})
                                          : InstructionBuilder(Instruction::simple(Opcode::Return).value()))
                        .value();
    EXPECT_TRUE(code.bind_label(target, CodePosition{backwards ? code.at(0) : terminal}));
    std::pmr::monotonic_buffer_resource scratch;
    auto result = encode_code(code, LEGACY, operands.pool, 0, false, &scratch, PATH);
    if (opcode == Opcode::IfEq && displacement > (backwards ? 32768u : 32767u)) {
      ASSERT_FALSE(result);
      EXPECT_EQ(result.error().kind, WriteErrorKind::BranchOutOfRange);
      return;
    }
    ASSERT_TRUE(result) << result.error().detail;
    auto bytecode = result->bytes();
    auto origin = backwards ? displacement : 0;
    if (opcode != Opcode::IfEq && displacement > (backwards ? 32768u : 32767u)) {
      auto wide = opcode == Opcode::Goto ? Opcode::GotoW : Opcode::JsrW;
      EXPECT_EQ(bytecode[origin], static_cast<std::byte>(wide));
    } else {
      EXPECT_EQ(bytecode[origin], static_cast<std::byte>(opcode));
    }
    MethodCode checked;
    EXPECT_TRUE(decode_code(checked, bytecode, 0, LEGACY, operands.pool, 0, {}, &scratch, PATH));
    compare_instructions(code, checked);
  };

  branch_example(Opcode::IfEq, 32767, false);
  branch_example(Opcode::IfEq, 32768, false);
  branch_example(Opcode::IfEq, 32768, true);
  branch_example(Opcode::IfEq, 32769, true);
  branch_example(Opcode::Goto, 32768, false);
  branch_example(Opcode::Jsr, 32769, true);

  MethodCode maximum;
  maximum.reserve(MAX_CODE_SIZE + 1);
  for (std::uint32_t i = 1; i < MAX_CODE_SIZE; ++i)
    maximum.append(Instruction{}).value();
  maximum.append(Instruction::simple(Opcode::Return).value()).value();
  std::pmr::monotonic_buffer_resource scratch;
  auto encoded = encode_code(maximum, MODERN, operands.pool, 0, false, &scratch, PATH);
  ASSERT_TRUE(encoded);
  EXPECT_EQ(encoded->bytes().size(), MAX_CODE_SIZE);
  MethodCode largest_decoded;
  auto boundaries = decode_code(largest_decoded, encoded->bytes(), 0, MODERN, operands.pool, 0, {}, &scratch, PATH);
  ASSERT_TRUE(boundaries);
  EXPECT_TRUE(boundaries->is_instruction_start(MAX_CODE_SIZE - 1));
  EXPECT_TRUE(boundaries->is_boundary(MAX_CODE_SIZE));
  EXPECT_FALSE(boundaries->is_instruction_start(MAX_CODE_SIZE));
  auto end = boundaries->label_at(largest_decoded, MAX_CODE_SIZE).value();
  EXPECT_EQ(largest_decoded.label_position(end).value(), CodePosition{});
  EXPECT_FALSE(largest_decoded.edited());
  maximum.append(Instruction{}).value();
  EXPECT_EQ(encode_code(maximum, MODERN, operands.pool, 0, false, &scratch, PATH).error().kind, WriteErrorKind::CodeTooLarge);
}

TEST(Bytecode, BranchWideningRecomputesSwitchAlignment) {
  Operands operands;
  MethodCode code;
  auto destination = code.make_label().value();
  ASSERT_TRUE(code.append(Branch{Opcode::Goto, destination}));
  const std::array<LabelId, 1> targets{destination};
  ASSERT_TRUE(code.append(TableSwitch{-11, destination, targets}));

  // The short jump would put the switch at 3 and the target at 32768.
  // Widening it moves the next instruction past two more padding bytes.
  for (unsigned i = 20; i < 32768; ++i)
    code.append(Instruction{}).value();
  auto tail = code.append(Instruction::simple(Opcode::Return).value()).value();
  ASSERT_TRUE(code.bind_label(destination, CodePosition{tail}));

  std::pmr::monotonic_buffer_resource scratch;
  auto encoded = encode_code(code, MODERN, operands.pool, 0, false, &scratch, PATH);
  ASSERT_TRUE(encoded) << encoded.error().detail;
  ASSERT_GE(encoded->instruction_offsets().size(), 3u);
  EXPECT_EQ(encoded->bytes().front(), static_cast<std::byte>(Opcode::GotoW));
  EXPECT_EQ(encoded->instruction_offsets()[1], 5u);
  EXPECT_EQ(encoded->instruction_offsets()[2], 24u);
  EXPECT_EQ(encoded->offset_of(destination), 32772u);
  MethodCode checked;
  ASSERT_TRUE(decode_code(checked, encoded->bytes(), 0, MODERN, operands.pool, 0, {}, &scratch, PATH));
  compare_instructions(code, checked);
}

TEST(Bytecode, BoundsSwitchCountAtTheCodeAndResourceLimits) {
  constexpr std::size_t CASES = 16379;
  Operands operands;
  MethodCode code;
  auto destination = code.make_label().value();
  std::vector<LabelId> targets(CASES, destination);
  auto table = code.append(TableSwitch{-2000, destination, targets}).value();
  auto tail = code.append(Instruction::simple(Opcode::Return).value()).value();
  ASSERT_TRUE(code.bind_label(destination, CodePosition{tail}));
  std::pmr::monotonic_buffer_resource scratch;
  auto encoded = encode_code(code, MODERN, operands.pool, 0, false, &scratch, PATH);
  ASSERT_TRUE(encoded) << encoded.error().detail;
  EXPECT_EQ(encoded->bytes().size(), 65533u);

  ReadOptions options;
  options.limits.max_switch_cases = CASES - 1;
  MethodCode rejected;
  auto limited = decode_code(rejected, encoded->bytes(), 0, MODERN, operands.pool, 0, options, &scratch, PATH);
  ASSERT_FALSE(limited);
  EXPECT_EQ(limited.error().kind, ReadErrorKind::ResourceLimitExceeded);

  options.limits.max_switch_cases = CASES;
  MethodCode accepted;
  ASSERT_TRUE(decode_code(accepted, encoded->bytes(), 0, MODERN, operands.pool, 0, options, &scratch, PATH));
  auto parsed = accepted.table_switch(accepted.switch_id(accepted.at(0)).value()).value();
  EXPECT_EQ(parsed.targets.size(), CASES);

  targets.push_back(destination);
  ASSERT_TRUE(code.replace(table, TableSwitch{-2000, destination, targets}));
  auto oversized = encode_code(code, MODERN, operands.pool, 0, false, &scratch, PATH);
  ASSERT_FALSE(oversized);
  EXPECT_EQ(oversized.error().kind, WriteErrorKind::CodeTooLarge);
}

TEST(Bytecode, EndBoundaryCannotBeAnExecutableTarget) {
  Operands operands;
  MethodCode code;
  auto label = code.make_label().value();
  EXPECT_TRUE(code.append(Branch{Opcode::Goto, label}));
  ASSERT_TRUE(code.bind_label(label, CodePosition{std::nullopt}));
  std::pmr::monotonic_buffer_resource scratch;
  auto encoded = encode_code(code, MODERN, operands.pool, 0, false, &scratch, PATH);
  ASSERT_FALSE(encoded);
  EXPECT_EQ(encoded.error().kind, WriteErrorKind::InvalidLabel);
}

} // namespace
} // namespace cafe
