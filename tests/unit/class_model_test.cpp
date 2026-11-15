#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory_resource>
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
const WriteOptions SUPPLIED{.code_policy = CodeWritePolicy::CallerSuppliedMetadata};

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

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
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
  const auto output = write_class(model);
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

TEST(ConstantPool, HandlesUnknownTagsWrongReferencesAndTerminalLong) {
  auto model = fixture();
  const auto output = write_class(model).value();
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
  EXPECT_EQ(write_class(model).error().kind, WriteErrorKind::InvalidConstantPoolReference);
}

TEST(ConstantPool, PreservesMalformedUnusedModifiedUtf8WithDiagnostics) {
  auto model = fixture();
  const auto index = text(model, "zyx-unused-utf8");
  auto bytes = write_class(model).value().bytes;
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
  const auto wrote = write_class(compatible->class_file);
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
  auto bytes = write_class(model).value().bytes;
  bytes[7] = std::byte{49};
  const auto strict = read_class(bytes, STRICT);
  ASSERT_FALSE(strict);
  EXPECT_EQ(strict.error().kind, ReadErrorKind::VersionInapplicable);
  const auto compatible = read_class(bytes);
  ASSERT_TRUE(compatible) << compatible.error().detail;
  ASSERT_EQ(compatible->diagnostics.size(), 1u);
  EXPECT_EQ(compatible->diagnostics[0].action, RecoveryAction::RetainedRaw);
  EXPECT_EQ(write_class(compatible->class_file).error().kind, WriteErrorKind::UnsupportedDialect);
}

TEST(ClassOwnership, MovesSourceWithoutCopyingAndPreservesOpaquePayloadOnAppend) {
  CountingResource upstream;
  {
    auto model = fixture();
    (void)with_return(model);
    const auto name = text(model, "Vendor");
    ASSERT_TRUE(model.add_attribute(Attribute::raw(name, {std::byte{0}, std::byte{4}})));
    auto written = write_class(model, SUPPLIED).value();
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
    const auto output = write_class(moved).value();
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
  auto bytes = write_class(model).value().bytes;
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
  const auto wrote = write_class(read->class_file).value();
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
  auto bytes = write_class(model).value().bytes;
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
  const auto repaired = write_class(compatible->class_file).value();
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
    auto bytes = write_class(model).value().bytes;
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
  const auto original = write_class(model).value().bytes;
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
  EXPECT_EQ(write_class(compatible->class_file).value().bytes, original);
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
  const auto input = write_class(model, SUPPLIED).value().bytes;
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

  const auto written = write_class(file);
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
  const auto golden = write_class(model, SUPPLIED).value().bytes;
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
  auto bytes = write_class(model).value().bytes;
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
  EXPECT_EQ(write_class(parsed, {.force_reencode = true}).value().bytes, bytes);
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
