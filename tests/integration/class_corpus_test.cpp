#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

namespace {

const std::filesystem::path FIXTURES(HIPPOCAFE_FIXTURE_DIR);
const cafe::ReadOptions STRICT{.mode = cafe::ParseMode::Strict};
const cafe::WriteOptions PRESERVE{.constant_pool = cafe::ConstantPoolWritePolicy::Preserve};
const cafe::WriteOptions REENCODE{.force_reencode = true, .constant_pool = cafe::ConstantPoolWritePolicy::Preserve};

std::vector<std::byte> load_file(const std::filesystem::path& path) {
  const auto size = static_cast<std::size_t>(std::filesystem::file_size(path));
  std::vector<std::byte> bytes(size);
  std::ifstream file(path, std::ios::binary);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
    throw std::runtime_error("Cannot read fixture " + path.string());
  }
  return bytes;
}

std::vector<std::filesystem::path> class_files(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".class") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  return files;
}

void check_roundtrip(const std::filesystem::path& path) {
  SCOPED_TRACE(path.string());
  const auto bytes = load_file(path);
  const auto parsed = cafe::read_class(bytes, STRICT);
  ASSERT_TRUE(parsed) << parsed.error().path << " at " << parsed.error().offset << ": " << parsed.error().detail;
  EXPECT_EQ(parsed->class_file.validity(), cafe::ClassValidity::ParsedClean);
  EXPECT_TRUE(parsed->diagnostics.empty());

  const auto output = cafe::write_class(parsed->class_file, PRESERVE);
  ASSERT_TRUE(output) << output.error().path << ": " << output.error().detail;
  EXPECT_TRUE(output->bytes == bytes) << "Preservation mode changed a clean fixture";

  const auto reencoded = cafe::write_class(parsed->class_file, REENCODE);
  ASSERT_TRUE(reencoded) << reencoded.error().path << ": " << reencoded.error().detail;
  EXPECT_TRUE(reencoded->bytes == bytes) << "Encoder failed to preserve a clean fixture's encoding";
  const auto reparsed = cafe::read_class(reencoded->bytes, STRICT);
  ASSERT_TRUE(reparsed) << reparsed.error().path << " at " << reparsed.error().offset << ": " << reparsed.error().detail;
  EXPECT_EQ(reparsed->class_file.version(), parsed->class_file.version());
  EXPECT_EQ(reparsed->class_file.constant_pool().size(), parsed->class_file.constant_pool().size());
  ASSERT_EQ(reparsed->class_file.methods().size(), parsed->class_file.methods().size());
  for (std::size_t i = 0; i < parsed->class_file.methods().size(); ++i) {
    const auto* first = parsed->class_file.methods()[i].code();
    const auto* second = reparsed->class_file.methods()[i].code();
    ASSERT_EQ(first != nullptr, second != nullptr);
    if (first) {
      EXPECT_EQ(first->code().size(), second->code().size());
      EXPECT_EQ(first->max_stack(), second->max_stack());
      EXPECT_EQ(first->max_locals(), second->max_locals());
      EXPECT_EQ(first->exception_handlers().size(), second->exception_handlers().size());
    }
  }
}

TEST(ClassCorpus, supplied_fixtures_roundtrip) {
  const auto files = class_files(FIXTURES / "supplied/classes");
  ASSERT_EQ(files.size(), 30u);
  for (const auto& file : files) {
    check_roundtrip(file);
  }
}

TEST(ClassCorpus, drops_replaced_literal_from_emitted_pool) {
  using namespace cafe;
  auto parsed = read_class(load_file(FIXTURES / "supplied/classes/HelloWorld.class"), STRICT);
  ASSERT_TRUE(parsed) << parsed.error().detail;
  auto& file = parsed->class_file;
  auto main = file.edit_method("main", "([Ljava/lang/String;)V").value();
  const auto original = main.find_first(pattern::opcode(Opcode::Ldc)).value()->start();
  const auto source_index = main.code()->code().get(original)->constant_index().value();
  constexpr std::string_view REPLACEMENT = "axecueOEc8290";
  ASSERT_TRUE(main.replace(original, insn::ldc(REPLACEMENT)));
  const auto written = write_class(file, {.code_analysis = CodeAnalysisMode::ComputeMaxs});
  ASSERT_TRUE(written) << written.error().detail;
  const auto reopened = read_class(written->bytes, STRICT);
  ASSERT_TRUE(reopened) << reopened.error().detail;

  const auto& output = reopened->class_file;
  const auto method = output.find_method("main", "([Ljava/lang/String;)V").value();
  const auto match = pattern::find_first(output, method, pattern::opcode(Opcode::Ldc)).value();
  const auto load = output.instruction({method, match->start()}).value().raw();
  const auto& pool = output.constant_pool();
  const auto* replacement = pool.get(*load.constant_index());
  ASSERT_NE(replacement, nullptr);
  EXPECT_EQ(replacement->tag, CpTag::String);
  EXPECT_EQ(pool.get_utf8(replacement->first), REPLACEMENT);
  for (std::size_t i = 1; i < pool.size(); ++i)
    EXPECT_NE(pool.get_utf8(CpIndex{static_cast<std::uint16_t>(i)}), "Hello World");
  const auto& source_pool = file.constant_pool();
  EXPECT_EQ(source_pool.get_utf8(source_pool.get(source_index)->first), "Hello World");
}

TEST(ClassCorpus, supported_methods_lower_and_relift) {
  // All methods in these checked-in classes have Code and need no external hierarchy.
  // Exercise every declaration, including constructors, with the default metadata policy.
  for (const auto name : {"generated/DynamicConstant.class", "generated/Early45_2.class",
                          "supplied/classes/ArrayTest.class", "supplied/classes/BranchTest.class",
                          "supplied/classes/ForLoopTest.class", "supplied/classes/ForLoopTestLong.class",
                          "supplied/classes/InvokeDynamicTest.class", "supplied/classes/JumpInit.class",
                          "supplied/classes/LookupSwitchTest.class", "supplied/classes/NestedForLoopTest.class",
                          "supplied/classes/TableSwitchTest.class", "supplied/classes/TernaryTest.class"}) {
    SCOPED_TRACE(name);
    auto parsed = cafe::read_class(load_file(FIXTURES / name), STRICT);
    ASSERT_TRUE(parsed) << parsed.error().detail;
    auto& owner = parsed->class_file;
    cafe::analysis::AnalysisContext context;
    ASSERT_FALSE(owner.methods().empty());
    for (std::size_t i = 0; i < owner.methods().size(); ++i) {
      SCOPED_TRACE(i);
      auto graph = cafe::analysis::lift_ssa(owner, i, context);
      ASSERT_TRUE(graph) << graph.error().detail;
      const auto lowered = cafe::analysis::lower_ssa(owner, *graph, context);
      ASSERT_TRUE(lowered) << lowered.error().detail;
      EXPECT_TRUE(lowered->diagnostics.empty());
    }
    const auto output = cafe::write_class(owner);
    ASSERT_TRUE(output) << output.error().detail;
    auto reopened = cafe::read_class(output->bytes, STRICT);
    ASSERT_TRUE(reopened) << reopened.error().detail;
    ASSERT_EQ(reopened->class_file.methods().size(), owner.methods().size());
    for (std::size_t i = 0; i < owner.methods().size(); ++i) {
      SCOPED_TRACE(i);
      auto graph = cafe::analysis::lift_ssa(reopened->class_file, i, context);
      ASSERT_TRUE(graph) << graph.error().detail;
      const auto verified = cafe::analysis::verify_ssa(reopened->class_file, *graph, context);
      ASSERT_TRUE(verified) << verified.error().detail;
    }
  }
}

TEST(ClassCorpus, early_versions_and_legacy_subroutines_roundtrip) {
  check_roundtrip(FIXTURES / "historical/OakTest.class");
  for (const auto name : {"Early45_0", "Early45_1", "Early45_2", "Early45_3", "LegacySubroutine"}) {
    check_roundtrip(FIXTURES / "generated" / (std::string(name) + ".class"));
  }
}

TEST(ClassCorpus, dynamic_constant_roundtrips) {
  check_roundtrip(FIXTURES / "generated/DynamicConstant.class");
}

TEST(ClassCorpus, inspects_symbolic_operands_in_a_compiled_lambda) {
  using namespace cafe;
  const auto parsed = read_class(load_file(FIXTURES / "supplied/classes/InvokeDynamicTest.class"), STRICT);
  ASSERT_TRUE(parsed);
  const auto& file = parsed->class_file;
  const auto calls = file.find_invocations("java/lang/Runnable", "run", "()V").value();
  ASSERT_EQ(calls.size(), 1);
  const auto invocation = file.inspect_instruction(calls.front());
  ASSERT_TRUE(invocation);
  EXPECT_EQ(std::get<MethodInvocation>(invocation->operand()).opcode, Opcode::InvokeInterface);
  const auto fields = file.find_field_accesses("java/lang/System", "out", "Ljava/io/PrintStream;").value();
  ASSERT_EQ(fields.size(), 1);
  const auto lambda_index = fields.front().method_index;
  const auto& lambda_code = file.methods()[lambda_index].code()->code();
  EXPECT_EQ(file.inspect_instruction({lambda_index, lambda_code.at(1)}).error(), CodeEditError::WrongOperandForm);

  const auto main = file.find_method("main", "([Ljava/lang/String;)V").value();
  const auto indy = file.inspect_instruction({main, file.methods()[main].code()->code().at(0)});
  ASSERT_TRUE(indy);
  const auto& call = std::get<DynamicInvocation>(indy->operand());
  EXPECT_EQ(call.name, "run");
  EXPECT_EQ(call.bootstrap.handle.owner, "java/lang/invoke/LambdaMetafactory");
  EXPECT_EQ(call.bootstrap.handle.name, "metafactory");
  const auto arguments = call.bootstrap.arguments;
  ASSERT_EQ(arguments.size(), 3);
  EXPECT_EQ(std::get<MethodTypeConstant>(arguments[0].value).descriptor, "()V");
  const auto& lambda = std::get<MethodHandle>(arguments[1].value);
  EXPECT_EQ(lambda.kind, MethodHandleKind::InvokeStatic);
  EXPECT_EQ(lambda.owner, "InvokeDynamicTest");
  EXPECT_EQ(lambda.name, "lambda$main$0");
  EXPECT_EQ(std::get<MethodTypeConstant>(arguments[2].value).descriptor, "()V");
}

TEST(ClassCorpus, dense_switches_and_many_opaque_attributes_roundtrip) {
  check_roundtrip(FIXTURES / "generated/SwitchHeavy.class");
  check_roundtrip(FIXTURES / "generated/ManyOpaqueAttributes.class");
}

TEST(ClassCorpus, old_and_modern_poisoned_annotation_payloads_recover) {
  for (const auto old : {false, true}) {
    SCOPED_TRACE(old ? "45.3 (inapplicable)" : "52.0 (malformed)");
    const auto path = old ? "generated/OldPoisonedAttribute.class" : "generated/ModernPoisonedAttribute.class";
    const auto bytes = load_file(FIXTURES / path);

    const auto strict_read = cafe::read_class(bytes, STRICT);
    ASSERT_FALSE(strict_read);
    EXPECT_EQ(strict_read.error().kind, old ? cafe::ReadErrorKind::VersionInapplicable : cafe::ReadErrorKind::MalformedAttribute);

    const auto compatible = cafe::read_class(bytes);
    ASSERT_TRUE(compatible) << compatible.error().detail;
    EXPECT_EQ(compatible->class_file.validity(), cafe::ClassValidity::ParsedRecovered);
    ASSERT_EQ(compatible->diagnostics.size(), 1u);
    const auto& diagnostic = compatible->diagnostics.front();
    EXPECT_EQ(diagnostic.action, cafe::RecoveryAction::Dropped);
    EXPECT_EQ(diagnostic.code, old ? cafe::DiagnosticCode::VersionInapplicable : cafe::DiagnosticCode::DroppedMalformedAttribute);

    const auto output = cafe::write_class(compatible->class_file, PRESERVE);
    ASSERT_TRUE(output) << output.error().detail;
    EXPECT_EQ(output->bytes.size() + 10, bytes.size());
    const auto reparsed = cafe::read_class(output->bytes, STRICT);
    ASSERT_TRUE(reparsed) << reparsed.error().detail;
    const auto attributes = reparsed->class_file.attributes();
    ASSERT_EQ(attributes.size(), 2u);
    EXPECT_EQ(attributes[0].kind(), cafe::AttributeKind::SourceFile);
    EXPECT_EQ(attributes[1].kind(), cafe::AttributeKind::Deprecated);
  }
}

TEST(ClassCorpus, attribute_overrun_is_fatal) {
  const auto bytes = load_file(FIXTURES / "generated/AttributeOverrun.class");
  for (const auto& options : {cafe::ReadOptions{}, STRICT}) {
    const auto result = cafe::read_class(bytes, options);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, cafe::ReadErrorKind::AttributeOverrun);
  }
}

TEST(ClassCorpus, reserved_opcode_requires_explicit_opaque_read_and_write) {
  const auto bytes = load_file(FIXTURES / "generated/ReservedOpcode.class");
  EXPECT_FALSE(cafe::read_class(bytes, STRICT));
  EXPECT_FALSE(cafe::read_class(bytes));
  const auto opaque = cafe::read_class(bytes, cafe::ReadOptions{.allow_opaque_code = true});
  ASSERT_TRUE(opaque) << opaque.error().detail;
  EXPECT_EQ(opaque->class_file.validity(), cafe::ClassValidity::OpaqueEssentialContent);
  ASSERT_EQ(opaque->class_file.methods().size(), 1u);
  EXPECT_EQ(opaque->class_file.methods()[0].code(), nullptr);
  cafe::analysis::AnalysisContext context;
  const auto lift = cafe::analysis::lift_ssa(opaque->class_file, 0, context);
  ASSERT_FALSE(lift);
  EXPECT_EQ(lift.error().kind, cafe::analysis::SsaErrorKind::UnsupportedOpaqueCode);
  const auto create = cafe::analysis::create_ssa(opaque->class_file, 0, context);
  ASSERT_FALSE(create);
  EXPECT_EQ(create.error().kind, cafe::analysis::SsaErrorKind::UnsupportedOpaqueCode);
  const auto refused = cafe::write_class(opaque->class_file);
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error().kind, cafe::WriteErrorKind::UnsupportedOpaqueCode);
  const auto compact = cafe::write_class(opaque->class_file, {.allow_opaque_code = true});
  ASSERT_FALSE(compact);
  EXPECT_EQ(compact.error().kind, cafe::WriteErrorKind::UnsupportedOpaqueCode);
  const auto preserved = cafe::write_class(opaque->class_file, {.allow_opaque_code = true,
                                                                .constant_pool = cafe::ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(preserved) << preserved.error().detail;
  EXPECT_TRUE(preserved->bytes == bytes);
}

TEST(ClassCorpus, every_truncated_prefix_of_small_fixtures_fails) {
  for (const auto name : {"supplied/classes/HelloWorld.class", "supplied/classes/TableSwitchTest.class", "generated/DynamicConstant.class"}) {
    SCOPED_TRACE(name);
    const auto bytes = load_file(FIXTURES / name);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
      SCOPED_TRACE(size);
      const auto prefix = std::span(bytes).first(size);
      EXPECT_FALSE(cafe::read_class(prefix, STRICT));
      EXPECT_FALSE(cafe::read_class(prefix));
    }
  }
}

TEST(ClassCorpus, optional_local_samples) {
  const auto files = class_files(FIXTURES / "local");
  if (files.empty()) {
    GTEST_SKIP() << "Local-only samples are absent";
  }
  for (const auto& path : files) {
    if (path.filename() == "InvalidOpcode.class") {
      SCOPED_TRACE(path.string());
      const auto bytes = load_file(path);
      EXPECT_FALSE(cafe::read_class(bytes, STRICT));
      const auto opaque = cafe::read_class(bytes, cafe::ReadOptions{.allow_opaque_code = true});
      ASSERT_TRUE(opaque) << opaque.error().path << ": " << opaque.error().detail;
      EXPECT_EQ(opaque->class_file.validity(), cafe::ClassValidity::OpaqueEssentialContent);
      EXPECT_FALSE(cafe::write_class(opaque->class_file));
    } else {
      check_roundtrip(path);
    }
  }
}

#ifdef HIPPOCAFE_JAVA_FIXTURE_DIR
TEST(ClassCorpus, javac_modern_features_roundtrip) {
  const auto files = class_files(HIPPOCAFE_JAVA_FIXTURE_DIR);
  ASSERT_EQ(files.size(), 6u);
  for (const auto& path : files) {
    check_roundtrip(path);

    SCOPED_TRACE(path.string());
    auto parsed = cafe::read_class(load_file(path), STRICT);
    ASSERT_TRUE(parsed) << parsed.error().detail;
    const auto compact = cafe::write_class(parsed->class_file);
    ASSERT_TRUE(compact) << compact.error().path << ": " << compact.error().detail;
    const auto reopened = cafe::read_class(compact->bytes, STRICT);
    ASSERT_TRUE(reopened) << reopened.error().path << ": " << reopened.error().detail;
    EXPECT_EQ(cafe::write_class(reopened->class_file).value().bytes, compact->bytes);

    auto& model = parsed->class_file;
    ASSERT_TRUE(model.constant_pool().append_utf8("unused before reclamation"));
    const auto reclaimed = model.compact_constant_pool();
    ASSERT_TRUE(reclaimed) << reclaimed.error().path << ": " << reclaimed.error().detail;
    ASSERT_GT(*reclaimed, 0u);
    const auto preserved = cafe::write_class(model, PRESERVE);
    ASSERT_TRUE(preserved) << preserved.error().path << ": " << preserved.error().detail;
    const auto reparsed = cafe::read_class(preserved->bytes, STRICT);
    ASSERT_TRUE(reparsed) << reparsed.error().path << ": " << reparsed.error().detail;
    EXPECT_EQ(cafe::write_class(reparsed->class_file).value().bytes, compact->bytes);
  }
}
#endif

#ifdef HIPPOCAFE_JRT_FIXTURE_DIR
TEST(ClassCorpus, openjdk_runtime_jar_roundtrips) {
  const auto files = class_files(HIPPOCAFE_JRT_FIXTURE_DIR);
  ASSERT_FALSE(files.empty());
  for (const auto& path : files) {
    check_roundtrip(path);
  }
}
#endif

} // namespace
