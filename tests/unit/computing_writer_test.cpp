#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

#include "io/binary_writer.hpp"

namespace cafe {
namespace {

TEST(ComputingWriter, MaximaWithoutFramesAndLegacyHeader) {
  for (const auto version : {ClassVersion{52, 0}, ClassVersion{45, 2}}) {
    auto owner = ClassFile::create(version, "Maxima").value();
    auto& pool = owner.constant_pool();
    const auto method = owner.add_method(0x0009, pool.append_utf8("sum").value(), pool.append_utf8("(JJ)J").value()).value();
    auto& code = owner.add_code(method, pool.append_utf8("Code").value()).value().get();
    ASSERT_TRUE(code.code().append(Instruction::lload(0)));
    ASSERT_TRUE(code.code().append(Instruction::lload(2)));
    ASSERT_TRUE(code.code().append(Instruction::simple(Opcode::LAdd).value()));
    ASSERT_TRUE(code.code().append(Instruction::simple(Opcode::LReturn).value()));

    const auto preserved = write_class(owner);
    ASSERT_FALSE(preserved);
    EXPECT_EQ(preserved.error().kind, WriteErrorKind::NeedsCodeMetadata);
    for (const auto mode : {CodeAnalysisMode::ComputeMaxs, CodeAnalysisMode::ComputeFrames}) {
      const auto wrote = write_class(owner, {.code_analysis = mode});
      ASSERT_TRUE(wrote) << wrote.error().detail;
      const auto reparsed = read_class(wrote->bytes, {.mode = ParseMode::Strict});
      ASSERT_TRUE(reparsed) << reparsed.error().detail;
      const auto* actual = reparsed->class_file.methods().front().code();
      ASSERT_NE(actual, nullptr);
      EXPECT_EQ(actual->max_stack(), 4);
      EXPECT_EQ(actual->max_locals(), 4);
      EXPECT_TRUE(actual->attributes().empty());
    }
    EXPECT_EQ(code.max_stack(), 0);
    EXPECT_EQ(code.max_locals(), 0);
  }
}

TEST(ComputingWriter, ReusesLocalsAcrossShortAndExtendedStackMapFrames) {
  auto owner = ClassFile::create({61, 0}, "RepeatedLocals").value();
  auto editor = owner.add_method(0x0009, "run", "(J)V").value();
  ASSERT_TRUE(editor.add_code());
  const auto end = editor.append(insn::return_()).value();
  ASSERT_TRUE(editor.insert_before(end, [](CodeBuilder& b) {
    const auto first = b.label();
    b.goto_(first);
    b.bind(first);
    for (int i = 0; i < 61; ++i)
      b.nop();
    const auto same = b.label();
    b.goto_(same);
    b.bind(same);
    b.lconst_0();
    const auto one = b.label();
    b.goto_(one);
    b.bind(one);
    b.pop2();
    for (int i = 0; i < 60; ++i)
      b.nop();
    b.lconst_0();
    const auto far_one = b.label();
    b.goto_(far_one);
    b.bind(far_one);
    b.pop2();
    for (int i = 0; i < 61; ++i)
      b.nop();
    const auto far_same = b.label();
    b.goto_(far_same);
    b.bind(far_same);
  }));
  const auto written = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames});
  ASSERT_TRUE(written) << written.error().detail;
  const auto parsed = read_class(written->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed);
  const auto attributes = parsed->class_file.methods()[0].code()->attributes();
  ASSERT_EQ(attributes.size(), 1U);
  const auto* raw = attributes.front().get<RawAttribute>();
  ASSERT_NE(raw, nullptr);
  const auto bytes = raw->bytes(parsed->class_file);
  ASSERT_TRUE(bytes);
  // Long occupies two slots but only one verification_type_info, both on the stack and in locals.
  const std::uint8_t expected[]{0, 5, 255, 0, 3, 0, 1, 4, 0, 0, 63, 67, 4, 247, 0, 64, 4, 251, 0, 64};
  EXPECT_TRUE(std::ranges::equal(*bytes, std::as_bytes(std::span(expected))));
}

TEST(ComputingWriter, RequiresFramesForEditedBranchAndDropsOpaqueOnlyOnRequest) {
  auto owner = ClassFile::create({52, 0}, "Opaque").value();
  auto& pool = owner.constant_pool();
  const auto method = owner.add_method(0x0009, pool.append_utf8("run").value(), pool.append_utf8("(I)V").value()).value();
  auto& attribute = owner.add_code(method, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  const auto label = code.make_label().value();
  ASSERT_TRUE(code.append(Instruction::iload(0)));
  ASSERT_TRUE(code.append(Instruction::branch(Opcode::IfEq, label).value()));
  const auto target = code.append(Instruction::return_void()).value();
  ASSERT_TRUE(code.bind_label(label, CodePosition{target}));
  std::vector<Attribute> nested;
  nested.push_back(Attribute::raw(pool.append_utf8("PrivateCodeOffsets").value(), {std::byte{1}}));
  attribute.supply_metadata(1, 1, {}, std::move(nested));

  const auto maxs = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_FALSE(maxs);
  EXPECT_EQ(maxs.error().kind, WriteErrorKind::NeedsFrameComputation);
  const auto unsafe = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames});
  ASSERT_FALSE(unsafe);
  EXPECT_EQ(unsafe.error().kind, WriteErrorKind::UnsafeOpaqueAttribute);
  const auto written = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames,
                                           .drop_unsafe_code_attributes = true});
  ASSERT_TRUE(written) << written.error().detail;
  ASSERT_EQ(written->diagnostics.size(), 1u);
  EXPECT_EQ(written->diagnostics[0].code, DiagnosticCode::DroppedUnsafeCodeAttribute);
  auto reparsed = read_class(written->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(reparsed) << reparsed.error().detail;
  const auto nested_attributes = reparsed->class_file.methods().front().code()->attributes();
  ASSERT_EQ(nested_attributes.size(), 1u);
  EXPECT_EQ(nested_attributes[0].kind(), AttributeKind::StackMapTable);
  const auto unchanged = write_class(reparsed->class_file, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(unchanged) << unchanged.error().detail;
  EXPECT_EQ(unchanged->bytes, written->bytes);

  auto& rewritten = reparsed->class_file.edit_code(method)->code();
  ASSERT_TRUE(rewritten.insert_before(rewritten.at(0), Instruction::nop()));
  const auto stale = write_class(reparsed->class_file);
  ASSERT_FALSE(stale);
  EXPECT_EQ(stale.error().kind, WriteErrorKind::StaleCodeMetadata);
  const auto stale_frames = write_class(reparsed->class_file, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_FALSE(stale_frames);
  EXPECT_EQ(stale_frames.error().kind, WriteErrorKind::NeedsFrameComputation);
  const auto updated = write_class(reparsed->class_file, {.code_analysis = CodeAnalysisMode::ComputeFrames});
  ASSERT_TRUE(updated) << updated.error().detail;
}

TEST(ComputingWriter, SubroutinesOnlyUsePreservation) {
  const auto file = std::filesystem::path(HIPPOCAFE_FIXTURE_DIR) / "generated" / "LegacySubroutine.class";
  std::ifstream input(file, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  const auto parsed = read_class(std::as_bytes(std::span(bytes)), {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  const auto preserve = write_class(parsed->class_file, {.constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(preserve) << preserve.error().detail;
  EXPECT_TRUE(std::ranges::equal(preserve->bytes, std::as_bytes(std::span(bytes))));
  for (const auto mode : {CodeAnalysisMode::ComputeMaxs, CodeAnalysisMode::ComputeFrames}) {
    const auto computed = write_class(parsed->class_file, {.code_analysis = mode});
    ASSERT_FALSE(computed);
    EXPECT_EQ(computed.error().kind, WriteErrorKind::UnsupportedSubroutineAnalysis);
  }
}

TEST(ComputingWriter, RejectsAmbiguousCatchAnnotationAfterHandlerDeletion) {
  auto owner = ClassFile::create({52, 0}, "RepeatedCatch").value();
  auto& pool = owner.constant_pool();
  const auto method = owner.add_method(0x0009, pool.append_utf8("run").value(), pool.append_utf8("()V").value()).value();
  auto& attribute = owner.add_code(method, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  const auto start = code.make_label().value();
  const auto target = code.make_label().value();
  const auto returning = code.append(Instruction::return_void()).value();
  const auto handling = code.append(Instruction::pop()).value();
  ASSERT_TRUE(code.append(Instruction::return_void()));
  ASSERT_TRUE(code.bind_label(start, CodePosition{returning}));
  ASSERT_TRUE(code.bind_label(target, CodePosition{handling}));
  const ExceptionHandler handler{start, target, target, {}};
  const std::array handlers{handler, handler};
  std::vector<std::byte> annotation;
  detail::BinaryWriter body(annotation);
  body.u2(2);
  const auto type = pool.append_utf8("LMarked;").value();
  for (std::uint16_t entry = 0; entry < 2; ++entry) {
    body.u1(0x42);
    body.u2(entry);
    body.u1(0);
    body.u2(type.value);
    body.u2(0);
  }
  std::vector<Attribute> attributes;
  attributes.push_back(Attribute::raw(pool.append_utf8("RuntimeVisibleTypeAnnotations").value(), std::move(annotation)));
  attribute.supply_metadata(1, 0, handlers, std::move(attributes));
  const WriteOptions options{.code_analysis = CodeAnalysisMode::ComputeFrames};
  const auto assembled = write_class(owner, options);
  ASSERT_TRUE(assembled) << assembled.error().detail;
  auto parsed = read_class(assembled->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto* compiled = parsed->class_file.edit_code(method);
  compiled->set_exception_handlers(compiled->exception_handlers().first(1));
  const auto unsafe = write_class(parsed->class_file, options);
  ASSERT_FALSE(unsafe);
  EXPECT_EQ(unsafe.error().kind, WriteErrorKind::UnmappedCodeOffset);
  const auto dropped = write_class(parsed->class_file, {.code_analysis = CodeAnalysisMode::ComputeFrames,
                                                        .drop_unsafe_code_attributes = true});
  ASSERT_TRUE(dropped) << dropped.error().detail;
  ASSERT_EQ(dropped->diagnostics.size(), 1u);
  EXPECT_EQ(dropped->diagnostics.front().code, DiagnosticCode::DroppedUnsafeCodeAttribute);
}

TEST(ComputingWriter, ChecksCrossPackageProtectedSuperclassReceiver) {
  classpath::ClassPathBuilder builder;
  const ClassVersion version{52, 0};
  const auto object = ClassFile::create(version, "java/lang/Object", std::nullopt).value();
  auto base = ClassFile::create(version, "base/Parent").value();
  auto& base_pool = base.constant_pool();
  ASSERT_TRUE(base.add_field(0x0004, base_pool.append_utf8("value").value(), base_pool.append_utf8("I").value()));
  ASSERT_TRUE(base.add_method(0x0004, base_pool.append_utf8("size").value(), base_pool.append_utf8("()I").value()));
  ASSERT_TRUE(base.add_method(0x0004, base_pool.append_utf8("<init>").value(), base_pool.append_utf8("()V").value()));
  const auto middle = ClassFile::create(version, "client/Intermediate", "base/Parent").value();
  ASSERT_TRUE(builder.add_class(object));
  ASSERT_TRUE(builder.add_class(base));
  ASSERT_TRUE(builder.add_class(middle));
  analysis::AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};

  for (const auto opcode : {Opcode::GetField, Opcode::InvokeVirtual, Opcode::InvokeSpecial}) {
    auto child = ClassFile::create(version, "client/Child", "client/Intermediate").value();
    auto& pool = child.constant_pool();
    const bool constructor = opcode == Opcode::InvokeSpecial;
    const bool field = opcode == Opcode::GetField;
    const auto reference_owner = constructor
                                     ? pool.append({CpTag::Class, 0, pool.append_utf8("base/Parent").value(), {}}).value()
                                     : child.super_class();
    const auto name = pool.append_utf8(constructor ? "<init>" : field ? "value"
                                                                      : "size")
                          .value();
    const auto desc = pool.append_utf8(constructor ? "()V" : field ? "I"
                                                                   : "()I")
                          .value();
    const auto signature = pool.append({CpTag::NameAndType, 0, name, desc}).value();
    const auto reference = pool.append({field ? CpTag::Fieldref : CpTag::Methodref, 0, reference_owner, signature}).value();
    const auto method = child.add_method(0x0009, pool.append_utf8("read").value(),
                                         pool.append_utf8(constructor ? "()V" : "(Lclient/Intermediate;)I").value())
                            .value();
    auto& code = child.add_code(method, pool.append_utf8("Code").value()).value().get().code();
    ASSERT_TRUE(code.append(constructor ? Instruction::constant(Opcode::New, reference_owner).value() : Instruction::aload(0)));
    ASSERT_TRUE(code.append(Instruction::constant(opcode, reference).value()));
    ASSERT_TRUE(code.append(Instruction::simple(constructor ? Opcode::Return : Opcode::IReturn).value()));

    const auto shape = analysis::compute_max_stack(child, child.methods()[method]);
    ASSERT_TRUE(shape) << shape.error().detail;
    const auto written = write_class(child, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context);
    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().kind, WriteErrorKind::InvalidVerificationFrame) << written.error().detail;
  }
}

} // namespace
} // namespace cafe
