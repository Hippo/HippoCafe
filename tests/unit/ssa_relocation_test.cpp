#include <algorithm>
#include <array>
#include <vector>

#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <cafe/class_file.hpp>
#include <cafe/io/class_reader.hpp>
#include <cafe/io/class_writer.hpp>

#include "io/binary_writer.hpp"
#include "io/code_encoder.hpp"
#include "io/opaque_attribute.hpp"
#include "io/ssa_relocation.hpp"

namespace cafe::detail {
namespace {

LabelId mark(MethodCode& code, std::size_t ordinal) {
  const auto label = code.make_label().value();
  const CodePosition position{ordinal == code.size() ? std::nullopt : std::optional{code.at(ordinal)}};
  code.bind_label(label, position).value();
  return label;
}

std::vector<std::byte> u2_fields(std::initializer_list<std::uint16_t> fields) {
  std::vector<std::byte> bytes;
  BinaryWriter writer(bytes);
  for (const auto value : fields)
    writer.u2(value);
  return bytes;
}

void annotation_suffix(BinaryWriter<std::vector<std::byte>>& writer, CpIndex type) {
  writer.u1(0);
  writer.u2(type.value);
  writer.u2(0);
}

TEST(SsaRelocation, PreservesDistinctLinesAlongAThreadedJumpChain) {
  auto owner = ClassFile::create({52, 0}, "LineScope").value();
  auto& pool = owner.constant_pool();
  const auto name = [&](std::string_view text) { return pool.append_utf8(text).value(); };
  const auto method = owner.add_method(0x0009, name("run"), name("()V")).value();
  auto& source = owner.add_code(method, name("Code")).value().get();
  auto& code = source.code();
  const auto via = code.make_label().value(), line = code.make_label().value(), exit = code.make_label().value();
  const auto ids = code.append(std::array<InstructionBuilder, 4>{
                                   Branch{Opcode::Goto, via}, Branch{Opcode::Goto, exit}, Instruction::return_void(), Branch{Opcode::Goto, line}})
                       .value();
  code.bind_label(via, {ids[3]}).value();
  code.bind_label(line, {ids[1]}).value();
  code.bind_label(exit, {ids[2]}).value();
  std::vector<Attribute> metadata;
  metadata.push_back(Attribute::raw(name("LineNumberTable"), u2_fields({2, 3, 42, 6, 43})));
  source.supply_metadata(0, 0, {}, std::move(metadata));

  analysis::AnalysisContext context;
  auto graph = analysis::lift_ssa(owner, method, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = analysis::lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;

  const auto& result = *owner.methods()[method].code();
  const auto& body = result.code();
  ASSERT_EQ(body.size(), 2u);
  EXPECT_EQ(body.get(body.at(0))->opcode(), Opcode::Goto);
  EXPECT_EQ(body.get(body.at(1))->opcode(), Opcode::Return);
  const auto attrs = result.attributes();
  const auto table = std::ranges::find(attrs, AttributeKind::LineNumberTable, &Attribute::kind);
  ASSERT_NE(table, attrs.end());
  EXPECT_TRUE(std::ranges::equal(table->get<RawAttribute>()->bytes(owner).value(), u2_fields({2, 0, 42, 3, 43})));
}

TEST(SsaRelocation, CompactsSparseWideDebugSlotAndIgnoresUnnamedTemporary) {
  auto owner = ClassFile::create({52, 0}, "SparseLocals").value();
  auto& pool = owner.constant_pool();
  const auto name = [&](std::string_view text) { return pool.append_utf8(text).value(); };
  const auto method = owner.add_method(0x0009, name("run"), name("()J")).value();
  auto& source = owner.add_code(method, name("Code")).value().get();
  for (const auto instruction : {Instruction::simple(Opcode::LConst1).value(), Instruction::lstore(200),
                                 Instruction::simple(Opcode::IConst2).value(), Instruction::istore(97),
                                 Instruction::lload(200), Instruction::simple(Opcode::LReturn).value()})
    source.code().append(instruction).value();
  const auto var = name("visible");
  const auto type = name("J");
  std::vector<Attribute> metadata;
  metadata.push_back(Attribute::raw(name("LocalVariableTable"), u2_fields({1, 3, 6, var.value, type.value, 200})));
  source.supply_metadata(2, 202, {}, std::move(metadata));

  analysis::AnalysisContext context;
  auto graph = analysis::lift_ssa(owner, method, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = analysis::lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  const auto bytes = write_class(owner);
  ASSERT_TRUE(bytes) << bytes.error().detail;
  auto parsed = read_class(bytes->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  const auto& result = *parsed->class_file.methods()[method].code();
  EXPECT_LE(result.max_locals(), 4u);
  const auto attrs = result.attributes();
  const auto table = std::ranges::find(attrs, AttributeKind::LocalVariableTable, &Attribute::kind);
  ASSERT_NE(table, attrs.end());
  const auto payload = table->get<RawAttribute>()->bytes(parsed->class_file).value();
  ASSERT_EQ(payload.size(), 12u);
  EXPECT_EQ(payload[10], std::byte{0});
  EXPECT_EQ(payload[11], std::byte{0});
  EXPECT_TRUE(std::ranges::any_of(result.code().instructions(), [](const auto& entry) {
    return entry.instruction.opcode() == Opcode::LStore && entry.instruction.local_index() == 0;
  }));
}

TEST(SsaRelocation, KeepsLocalScopeThatContainsOnlyAnEliminatedLoad) {
  auto owner = ClassFile::create({52, 0}, "LocalScope").value();
  auto& pool = owner.constant_pool();
  const auto name = [&](std::string_view text) { return pool.append_utf8(text).value(); };
  const auto index = owner.add_method(0x0009, name("identity"), name("(I)I")).value();
  auto& source = owner.add_code(index, name("Code")).value().get();
  ASSERT_TRUE(source.code().append(Instruction::iload(0)));
  ASSERT_TRUE(source.code().append(Instruction::simple(Opcode::IReturn).value()));
  std::vector<Attribute> metadata;
  metadata.push_back(Attribute::raw(name("LocalVariableTable"), u2_fields({1, 0, 1, name("argument").value, name("I").value, 0})));
  source.supply_metadata(1, 1, {}, std::move(metadata));
  analysis::AnalysisContext context;
  auto graph = analysis::lift_ssa(owner, index, context);
  ASSERT_TRUE(graph) << graph.error().detail;
  auto lowered = analysis::lower_ssa(owner, *graph, context);
  ASSERT_TRUE(lowered) << lowered.error().detail;
  const auto wire = write_class(owner);
  ASSERT_TRUE(wire) << wire.error().detail;
  const auto reparsed = read_class(wire->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  const auto& parsed_owner = reparsed->class_file;
  const auto attrs = parsed_owner.methods()[index].code()->attributes();
  const auto found = std::ranges::find(attrs, AttributeKind::LocalVariableTable, &Attribute::kind);
  ASSERT_NE(found, attrs.end());
  const auto bytes = found->get<RawAttribute>()->bytes(parsed_owner).value();
  ASSERT_EQ(bytes.size(), 12u);
  EXPECT_EQ(bytes[4], std::byte{0});
  EXPECT_EQ(bytes[5], std::byte{0});
}

TEST(SsaRelocation, RewritesDebugSlotsInstructionTargetsAndDuplicatedCatchRows) {
  auto owner = ClassFile::create({52, 0}, "SsaMetadata").value();
  auto& pool = owner.constant_pool();
  const auto name = [&](std::string_view text) { return pool.append_utf8(text).value(); };
  const auto method = owner.add_method(0x0009, name("m"), name("(Ljava/lang/Object;)V")).value();
  auto& source = owner.add_code(method, name("Code")).value().get();
  auto& code = source.code();
  const auto local_name = name("v");
  const auto descriptor = name("Ljava/lang/Object;");
  const auto annotation_type = name("LMarker;");
  const auto checked = Instruction::constant(Opcode::CheckCast, owner.super_class()).value();
  for (const auto op : {Instruction::aload(0), checked, Instruction::pop(), Instruction::return_void()})
    ASSERT_TRUE(code.append(op));

  std::vector<Attribute> attributes;
  attributes.push_back(Attribute::raw(name("LineNumberTable"), u2_fields({1, 1, 37})));
  for (const auto kind : {"LocalVariableTable", "LocalVariableTypeTable"})
    attributes.push_back(Attribute::raw(name(kind), u2_fields({1, 0, 6, local_name.value, descriptor.value, 0})));
  const auto annotations = [&](bool relocated) {
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writer.u2(relocated ? 4 : 3);
    writer.u1(0x40);
    writer.bytes(relocated ? u2_fields({2, 0, 5, 1, 5, 3, 2}) : u2_fields({1, 0, 6, 0}));
    annotation_suffix(writer, annotation_type);
    writer.u1(0x47);
    writer.u2(relocated ? 2 : 1);
    writer.u1(0);
    annotation_suffix(writer, annotation_type);
    for (int row = 0; row < (relocated ? 2 : 1); ++row) {
      writer.u1(0x42);
      writer.u2(static_cast<std::uint16_t>(row));
      annotation_suffix(writer, annotation_type);
    }
    return bytes;
  };
  attributes.push_back(Attribute::raw(name("RuntimeVisibleTypeAnnotations"), annotations(false)));
  const auto old_begin = mark(code, 0);
  const auto old_target = mark(code, 3);
  const std::array handlers{ExceptionHandler{old_begin, old_target, old_target, {}}};
  source.supply_metadata(1, 1, handlers, std::move(attributes));

  MethodCode staged;
  for (const auto op : {Instruction::nop(), Instruction::aload(1), checked, Instruction::pop(), Instruction::nop(), Instruction::return_void()})
    ASSERT_TRUE(staged.append(op));
  std::vector<LabelId> labels;
  for (std::size_t i = 0; i <= staged.size(); ++i)
    labels.push_back(mark(staged, i));
  SsaCodeProvenance map;
  map.instructions = {{code.at(0), labels[0], labels[2], staged.at(1)},
                      {code.at(1), labels[2], labels[3], staged.at(2)},
                      {code.at(2), labels[3], labels[5], staged.at(3)},
                      {code.at(3), labels[5], labels[6], staged.at(5)}};
  map.locals = {{0, 1, labels[0], labels[3]}, {0, 2, labels[3], labels[6]}};
  map.handler_sources = {0, 0};
  std::pmr::monotonic_buffer_resource scratch;
  auto encoded = encode_code(staged, owner.version(), pool, 3, true, &scratch, "code").value();
  const CodeAttributeContext context{encoded.bytes(), encoded.instruction_starts(), 3, 2};
  std::vector<Diagnostic> diagnostics;
  auto relocated = relocate_ssa_attributes(owner, source, staged, encoded, map, context, false, diagnostics, &scratch, "code", {});
  ASSERT_TRUE(relocated) << relocated.error().detail;
  ASSERT_EQ(relocated->size(), 4u);
  const auto expect_body = [&](std::size_t index, AttributeKind kind, const std::vector<std::byte>& expected) {
    const auto& attribute = (*relocated)[index];
    EXPECT_EQ(attribute.kind(), kind);
    EXPECT_TRUE(std::ranges::equal(attribute.get<RawAttribute>()->bytes(owner).value(), expected));
  };
  expect_body(0, AttributeKind::LineNumberTable, u2_fields({1, 2, 37}));
  const auto locals = u2_fields({2, 0, 5, local_name.value, descriptor.value, 1, 5, 3, local_name.value, descriptor.value, 2});
  expect_body(1, AttributeKind::LocalVariableTable, locals);
  expect_body(2, AttributeKind::LocalVariableTypeTable, locals);
  expect_body(3, AttributeKind::RuntimeVisibleTypeAnnotations, annotations(true));
  EXPECT_TRUE(diagnostics.empty());

  const auto wire = write_class(owner, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata,
                                        .constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(wire) << wire.error().detail;
  auto parsed = read_class(wire->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto& parsed_owner = parsed->class_file;
  const auto& parsed_source = *parsed_owner.methods()[method].code();
  auto parsed_map = map;
  for (std::size_t i = 0; i < parsed_map.instructions.size(); ++i)
    parsed_map.instructions[i].source = parsed_source.code().at(i);
  const auto from_wire = relocate_ssa_attributes(parsed_owner, parsed_source, staged, encoded, parsed_map, context, false,
                                                 diagnostics, &scratch, "code", {});
  ASSERT_TRUE(from_wire) << from_wire.error().detail;
  ASSERT_EQ(from_wire->size(), relocated->size());
  for (std::size_t i = 0; i < from_wire->size(); ++i)
    EXPECT_TRUE(std::ranges::equal((*from_wire)[i].get<RawAttribute>()->bytes(parsed_owner).value(),
                                   (*relocated)[i].get<RawAttribute>()->bytes(owner).value()));

  map.instructions[1].exact.reset();
  auto unsafe = relocate_ssa_attributes(owner, source, staged, encoded, map, context, false, diagnostics, &scratch, "code", {});
  ASSERT_FALSE(unsafe);
  EXPECT_EQ(unsafe.error().kind, WriteErrorKind::UnmappedCodeOffset);
  auto dropped = relocate_ssa_attributes(owner, source, staged, encoded, map, context, true, diagnostics, &scratch, "code", {});
  ASSERT_TRUE(dropped);
  EXPECT_EQ(dropped->size(), 3u);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::DroppedUnsafeCodeAttribute);
  EXPECT_EQ(diagnostics.front().action, RecoveryAction::Dropped);

  analysis::AnalysisLimits limits;
  limits.max_work = 1;
  auto exhausted = relocate_ssa_attributes(owner, source, staged, encoded, map, context, true, diagnostics, &scratch, "code", limits);
  ASSERT_FALSE(exhausted);
  EXPECT_EQ(exhausted.error().kind, WriteErrorKind::ResourceLimitExceeded);
  EXPECT_EQ(diagnostics.size(), 1u);
}

} // namespace
} // namespace cafe::detail
