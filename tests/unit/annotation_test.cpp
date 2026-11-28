#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cafe/io/class_reader.hpp>
#include <cafe/io/class_writer.hpp>
#include <gtest/gtest.h>

#include "io/attribute_registry.hpp"
#include "io/binary_writer.hpp"

namespace cafe {
namespace {

using Bytes = std::vector<std::byte>;
using Writer = detail::BinaryWriter<Bytes>;
using detail::AttributeContext;

enum FixtureConstant : std::uint16_t {
  CLASS_NAME = 1,
  THIS_CLASS,
  SUPER_NAME,
  SUPER_CLASS,
  CODE_NAME,
  METHOD_NAME,
  METHOD_DESCRIPTOR,
  BEFORE_NAME,
  AFTER_NAME,
  ATTRIBUTE_NAME,
  ANNOTATION_TYPE,
  ELEMENT_NAME,
};

Bytes bytes(std::initializer_list<std::uint8_t> values) {
  Bytes result;
  for (auto value : values) {
    result.push_back(std::byte{value});
  }
  return result;
}

void write_utf8(Writer& writer, std::string_view value) {
  writer.u1(1);
  writer.u2(static_cast<std::uint16_t>(value.size()));
  writer.bytes(std::as_bytes(std::span(value)));
}

void write_attribute(Writer& writer, std::uint16_t name, std::span<const std::byte> body,
                     std::optional<std::uint32_t> declared_length = std::nullopt) {
  writer.u2(name);
  writer.u4(declared_length.value_or(static_cast<std::uint32_t>(body.size())));
  writer.bytes(body);
}

// The one method has a two-byte bipush instruction, pop and return.
// Both vendor attributes must survive recovery from the attribute between them.
Bytes fixture(AttributeContext context, std::string_view name,
              std::optional<std::span<const std::byte>> body,
              std::optional<std::uint32_t> declared_length = std::nullopt) {
  Bytes result;
  Writer writer(result);
  writer.u4(CLASS_MAGIC);
  writer.u2(0);
  writer.u2(52);
  writer.u2(ELEMENT_NAME + 1);
  write_utf8(writer, "AnnotationFixture");
  writer.u1(7);
  writer.u2(CLASS_NAME);
  write_utf8(writer, "java/lang/Object");
  writer.u1(7);
  writer.u2(SUPER_NAME);
  for (auto value : std::array<std::string_view, 8>{
           "Code", "test", "()V", "VendorBefore", "VendorAfter", name, "LExample;", "value"}) {
    write_utf8(writer, value);
  }
  writer.u2(0x0021);
  writer.u2(THIS_CLASS);
  writer.u2(SUPER_CLASS);
  writer.u2(0); // interfaces
  writer.u2(0); // fields
  writer.u2(1); // methods
  writer.u2(0x0009);
  writer.u2(METHOD_NAME);
  writer.u2(METHOD_DESCRIPTOR);
  writer.u2(context == AttributeContext::Method ? (body ? 4 : 3) : 1);

  const auto write_neighbors = [&](Writer& output) {
    write_attribute(output, BEFORE_NAME, bytes({0xca, 0xfe}));
    if (body) {
      write_attribute(output, ATTRIBUTE_NAME, *body, declared_length);
    }
    write_attribute(output, AFTER_NAME, bytes({}));
  };
  Bytes code;
  Writer code_writer(code);
  code_writer.u2(1);
  code_writer.u2(1);
  code_writer.u4(4);
  code_writer.bytes(bytes({0x10, 0x01, 0x57, 0xb1}));
  code_writer.u2(0);
  if (context == AttributeContext::Code) {
    code_writer.u2(body ? 3 : 2);
    write_neighbors(code_writer);
  } else {
    code_writer.u2(0);
  }
  write_attribute(writer, CODE_NAME, code);
  if (context == AttributeContext::Method) {
    write_neighbors(writer);
  }
  if (context == AttributeContext::Class) {
    writer.u2(body ? 3 : 2);
    write_neighbors(writer);
  } else {
    writer.u2(0);
  }
  return result;
}

ReadOptions strict_options() {
  return ReadOptions{.mode = ParseMode::Strict};
}

void verify_recovery(AttributeContext context, std::string_view name, std::span<const std::byte> malformed,
                     ReadErrorKind expected = ReadErrorKind::MalformedAttribute) {
  const auto input = fixture(context, name, malformed);
  const auto strict = read_class(input, strict_options());
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, expected);
  EXPECT_NE(strict.error().path.find(name), std::string::npos);

  const auto compatible = read_class(input);
  ASSERT_TRUE(compatible) << compatible.error().path << ": " << compatible.error().detail;
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics.front().action, RecoveryAction::Dropped);
  EXPECT_EQ(compatible->diagnostics.front().code, DiagnosticCode::DroppedMalformedAttribute);
  const auto output = write_class(compatible->class_file, {.constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(output) << output.error().detail;
  EXPECT_EQ(output->bytes, fixture(context, name, std::nullopt));
  EXPECT_TRUE(read_class(output->bytes, strict_options()));
}

TEST(AttributeRegistry, PreviewVersionsAndContexts) {
  const auto* record = detail::find_attribute("Record");
  const auto* sealed = detail::find_attribute("PermittedSubclasses");
  ASSERT_NE(record, nullptr);
  ASSERT_NE(sealed, nullptr);
  EXPECT_TRUE(record->applicable_to({58, 65535}, AttributeContext::Class));
  EXPECT_TRUE(record->applicable_to({59, 65535}, AttributeContext::Class));
  EXPECT_TRUE(record->applicable_to({60, 0}, AttributeContext::Class));
  EXPECT_FALSE(record->applicable_to({58, 0}, AttributeContext::Class));
  EXPECT_FALSE(record->applicable_to({60, 0}, AttributeContext::Method));
  EXPECT_TRUE(sealed->applicable_to({59, 65535}, AttributeContext::Class));
  EXPECT_FALSE(sealed->applicable_to({58, 65535}, AttributeContext::Class));
  EXPECT_FALSE(sealed->applicable_to({60, 0}, AttributeContext::Class));
  EXPECT_TRUE(sealed->applicable_to({61, 0}, AttributeContext::Class));
  EXPECT_EQ(detail::find_attribute("record"), nullptr);
}

TEST(AttributeRegistry, PermitsRepeatableMarkersAndDebugTables) {
  for (auto name : {"Deprecated", "Synthetic", "LineNumberTable", "LocalVariableTable", "LocalVariableTypeTable"}) {
    const auto* spec = detail::find_attribute(name);
    ASSERT_NE(spec, nullptr) << name;
    EXPECT_TRUE(spec->repeatable) << name;
  }
  EXPECT_FALSE(detail::find_attribute("SourceFile")->repeatable);
}

TEST(OpaqueAttributes, DropsMalformedMethodAnnotations) {
  verify_recovery(AttributeContext::Method, "RuntimeVisibleParameterAnnotations",
                  bytes({1, 0xff, 0xff}));
  verify_recovery(AttributeContext::Method, "RuntimeInvisibleParameterAnnotations",
                  bytes({1, 0, 1, 0, THIS_CLASS, 0, 0}), ReadErrorKind::InvalidIndex);
}

TEST(OpaqueAttributes, DropsCodeTypeAnnotationsWithInvalidTarget) {
  const auto catch_without_an_exception_entry = bytes({0, 1, 0x42, 0, 0, 0, 0, ANNOTATION_TYPE, 0, 0});
  const auto cast_into_the_middle_of_bipush = bytes({0, 1, 0x47, 0, 1, 0, 0, 0, ANNOTATION_TYPE, 0, 0});
  verify_recovery(AttributeContext::Code, "RuntimeInvisibleTypeAnnotations", catch_without_an_exception_entry);
  verify_recovery(AttributeContext::Code, "RuntimeVisibleTypeAnnotations", cast_into_the_middle_of_bipush);
}

TEST(OpaqueAttributes, DropsMalformedDebugTable) {
  verify_recovery(AttributeContext::Code, "LineNumberTable", bytes({0, 1, 0, 4, 0, 1}));
  verify_recovery(AttributeContext::Code, "LocalVariableTable",
                  bytes({0, 1, 0, 1, 0, 2, 0, ELEMENT_NAME, 0, METHOD_DESCRIPTOR, 0, 0}));
  verify_recovery(AttributeContext::Code, "LocalVariableTable",
                  bytes({0, 1, 0, 0, 0, 2, 0, ELEMENT_NAME, 0, METHOD_DESCRIPTOR, 0, 0}));
}

TEST(OpaqueAttributes, RejectsAnnotationDefaultOnOrdinaryMethod) {
  const auto invalid = fixture(AttributeContext::Method, "AnnotationDefault", bytes({'s', 0, ELEMENT_NAME}));
  const auto strict = read_class(invalid, strict_options());
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::InvalidAttributeContext);
  const auto compatible = read_class(invalid);
  ASSERT_TRUE(compatible);
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics.front().code, DiagnosticCode::InvalidAttributeContext);
  const auto output = write_class(compatible->class_file, {.constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(output);
  EXPECT_EQ(output->bytes, fixture(AttributeContext::Method, "AnnotationDefault", std::nullopt));
}

TEST(OpaqueAttributes, RejectsUnboundedNestedAttributeEvenWithOpaqueCodeEnabled) {
  const auto input = fixture(AttributeContext::Code, "LineNumberTable", bytes({0, 0}),
                             std::numeric_limits<std::uint32_t>::max());
  const auto compatible = read_class(input, ReadOptions{.allow_opaque_code = true});
  ASSERT_FALSE(compatible);
  EXPECT_EQ(compatible.error().kind, ReadErrorKind::AttributeOverrun);
  const auto strict = read_class(input, strict_options());
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::AttributeOverrun);
}

TEST(OpaqueAttributes, ChecksStackMapOffsetsAndFrameTypes) {
  verify_recovery(AttributeContext::Code, "StackMapTable", bytes({0, 1, 200}));
  verify_recovery(AttributeContext::Code, "StackMapTable", bytes({0, 1, 1}));
  verify_recovery(AttributeContext::Code, "StackMapTable", bytes({0, 1, 66, 8, 0, 2}));

  const auto input = fixture(AttributeContext::Code, "StackMapTable", bytes({0, 1, 66, 1}));
  const auto parsed = read_class(input, strict_options());
  ASSERT_TRUE(parsed) << parsed.error().detail;
  const auto output = write_class(parsed->class_file, {.force_reencode = true, .constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(output) << output.error().detail;
  EXPECT_EQ(output->bytes, input);
}

TEST(OpaqueAttributes, BoundsDeepAnnotationNesting) {
  constexpr std::size_t NESTING = 300;
  Bytes body;
  Writer writer(body);
  writer.u2(1);
  writer.u2(ANNOTATION_TYPE);
  writer.u2(1);
  writer.u2(ELEMENT_NAME);
  for (std::size_t depth = 0; depth < NESTING; ++depth) {
    writer.u1('[');
    writer.u2(1);
  }
  writer.u1('s');
  writer.u2(ELEMENT_NAME);

  const auto input = fixture(AttributeContext::Class, "RuntimeVisibleAnnotations", body);
  auto limits = strict_options();
  limits.limits.max_annotation_depth = NESTING;
  const auto rejected = read_class(input, limits);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, ReadErrorKind::ResourceLimitExceeded);
  limits.mode = ParseMode::Compatible;
  EXPECT_FALSE(read_class(input, limits));
  limits.limits.max_annotation_depth = NESTING + 1;
  const auto accepted = read_class(input, limits);
  ASSERT_TRUE(accepted) << accepted.error().detail;
  EXPECT_TRUE(accepted->diagnostics.empty());
  const auto output = write_class(accepted->class_file, {.constant_pool = ConstantPoolWritePolicy::Preserve});
  ASSERT_TRUE(output) << output.error().detail;
  EXPECT_EQ(output->bytes, input);
}

} // namespace
} // namespace cafe
