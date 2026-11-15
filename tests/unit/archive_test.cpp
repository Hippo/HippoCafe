#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <type_traits>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

#include "archive/zip_archive.hpp"
#include "archive/zip_format.hpp"

namespace {

using namespace cafe::archive;
using namespace std::string_view_literals;
using cafe::classpath::SourceKind;
using Bytes = std::vector<std::byte>;

static_assert(std::is_move_constructible_v<Archive>);
static_assert(!std::is_copy_constructible_v<Archive>);

Bytes bytes(std::string_view text) {
  const auto data = std::as_bytes(std::span(text));
  return {data.begin(), data.end()};
}

void append(Bytes& out, std::initializer_list<std::uint32_t> fields, unsigned width) {
  for (const auto value : fields)
    for (unsigned i = 0; i < width; ++i)
      out.push_back(static_cast<std::byte>(value >> (8 * i)));
}

void append(Bytes& out, const Bytes& data) {
  out.insert(out.end(), data.begin(), data.end());
}

// Two externally framed stored entries in reverse central-directory order.
Bytes metadata_fixture() {
  constexpr std::uint32_t LOCAL_TIME = 0x54610001;
  constexpr std::uint32_t CENTRAL_TIME = 0x54610002;
  constexpr std::uint32_t CRC_X = 0x8cdc1683;
  const auto extra = bytes("\x55\x54\x05\0\x01\x11\x22\x33\x44"sv);
  const auto comment = bytes("e\0c"sv);
  Bytes out;
  std::array<Bytes, 2> central;
  for (std::size_t i = 0; i < central.size(); ++i) {
    const auto name = bytes(i == 0 ? "a" : "b");
    const auto offset = static_cast<std::uint32_t>(out.size());
    append(out, {detail::LOCAL_HEADER_MAGIC}, 4);
    append(out, {20, 0x0800, 0}, 2);
    append(out, {LOCAL_TIME, CRC_X, 1, 1}, 4);
    append(out, {1, static_cast<std::uint32_t>(extra.size())}, 2);
    append(out, name);
    append(out, extra);
    append(out, bytes("x"));

    auto& record = central[i];
    append(record, {detail::CENTRAL_HEADER_MAGIC}, 4);
    append(record, {0x0314, 20, 0x0800, 0}, 2);
    append(record, {CENTRAL_TIME, CRC_X, 1, 1}, 4);
    append(record, {1, static_cast<std::uint32_t>(extra.size()), static_cast<std::uint32_t>(comment.size()), 0, 1}, 2);
    append(record, {0100751u << 16, offset}, 4);
    append(record, name);
    append(record, extra);
    append(record, comment);
  }
  const auto directory = static_cast<std::uint32_t>(out.size());
  append(out, central[1]);
  append(out, central[0]);
  const auto directory_size = static_cast<std::uint32_t>(out.size()) - directory;
  append(out, {detail::EOCD_MAGIC}, 4);
  append(out, {0, 0, 2, 2}, 2);
  append(out, {directory_size, directory}, 4);
  append(out, {6}, 2);
  append(out, bytes("z\0PK\x05\x06"sv));
  return out;
}

class ArchiveTest : public testing::Test {
protected:
  void SetUp() override {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    directory = std::filesystem::temp_directory_path() / ("hippocafe-archives-" + std::to_string(now));
    ASSERT_TRUE(std::filesystem::create_directory(directory));
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
  void put(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    ASSERT_TRUE(stream);
  }
  std::filesystem::path directory;
};

TEST(Archive, EditsAreVisibleBeforeWritingAndRoundTrip) {
  auto jar = Archive::create(Format::Jar);
  ASSERT_TRUE(jar.add("one", bytes("stored"), {Compression::Store, {}}));
  ASSERT_TRUE(jar.add("remove", {}));
  ASSERT_TRUE(jar.add_directory("nested/"));
  ASSERT_TRUE(jar.add("nested/\xc3\xa9.txt", bytes("deflate")));
  ASSERT_TRUE(jar.add("empty", {}));
  EXPECT_EQ(jar.format(), Format::Jar);
  EXPECT_EQ(jar.add("one", {}).error().kind, ErrorKind::EntryExists);
  EXPECT_EQ(jar.replace("absent", {}).error().kind, ErrorKind::EntryNotFound);
  EXPECT_EQ(jar.replace("nested/", {}).error().kind, ErrorKind::EntryIsDirectory);
  EXPECT_EQ(jar.read("nested/").error().kind, ErrorKind::EntryIsDirectory);
  ASSERT_TRUE(jar.erase("remove"));
  ASSERT_TRUE(jar.erase("nested/"));
  ASSERT_TRUE(jar.replace("one", bytes("updated")));
  ASSERT_EQ(jar.entries().size(), 3u);
  EXPECT_EQ(jar.entries()[0].name, "one");
  EXPECT_EQ(jar.entries()[1].name, "nested/\xc3\xa9.txt");
  ASSERT_NE(jar.find("one"), nullptr);
  EXPECT_EQ(jar.find("one")->uncompressed_size, 7u);
  EXPECT_EQ(jar.read("one"), bytes("updated"));

  const auto encoded = jar.write();
  ASSERT_TRUE(encoded) << encoded.error().detail;
  EXPECT_EQ(jar.write(), *encoded);
  const auto zip = ZipArchive::open(*encoded, SourceKind::Jar, {});
  ASSERT_TRUE(zip) << zip.error().detail;
  EXPECT_FALSE(zip->find("one")->is_deflated());
  EXPECT_TRUE(zip->find("nested/\xc3\xa9.txt")->is_deflated());

  auto reopened = Archive::open(*encoded, Format::Jar);
  ASSERT_TRUE(reopened) << reopened.error().detail;
  EXPECT_EQ(reopened->read("one"), bytes("updated"));
  EXPECT_EQ(reopened->read("nested/\xc3\xa9.txt"), bytes("deflate"));
  EXPECT_EQ(reopened->read("empty"), Bytes{});
  EXPECT_EQ(reopened->read("remove").error().kind, ErrorKind::EntryNotFound);
  EXPECT_EQ(reopened->write(), *encoded);
}

TEST(Archive, RejectsUnsafeNamesAndRequiresAJmodLayoutAtWriteTime) {
  auto jar = Archive::create(Format::Jar);
  for (auto name : {"", "/", "../x", "a/./b", "a//b", "a\\b", "C:x", "\xc0\xaf"})
    EXPECT_EQ(jar.add(name, {}).error().kind, ErrorKind::InvalidName) << name;
  EXPECT_EQ(jar.add("x\0y"sv, {}).error().kind, ErrorKind::InvalidName);
  EXPECT_EQ(jar.add("dir/", {}).error().kind, ErrorKind::InvalidName);
  EXPECT_EQ(jar.add_directory("dir").error().kind, ErrorKind::InvalidName);
  EXPECT_EQ(jar.add_directory("dir/", 01000).error().kind, ErrorKind::InvalidOptions);
  auto empty = jar.write();
  ASSERT_TRUE(empty);
  EXPECT_TRUE(Archive::open(*empty, Format::Jar));
  EXPECT_EQ(Archive::open(*empty, Format::Jmod).error().kind, ErrorKind::InvalidArchive);

  auto jmod = Archive::create(Format::Jmod);
  EXPECT_EQ(jmod.write().error().kind, ErrorKind::InvalidJmodLayout);
  ASSERT_TRUE(jmod.add("classes/module-info.class", bytes("descriptor")));
  ASSERT_TRUE(jmod.add("unknown/resource", {}));
  EXPECT_EQ(jmod.write().error().entry, "unknown/resource");
  ASSERT_TRUE(jmod.erase("unknown/resource"));
  ASSERT_TRUE(jmod.add_directory("lib/"));
  ASSERT_TRUE(jmod.add("lib/native", bytes("library")));
  auto encoded = jmod.write();
  ASSERT_TRUE(encoded) << encoded.error().detail;
  EXPECT_TRUE(std::ranges::equal(std::span(*encoded).first(4), detail::JMOD_MAGIC));
  auto opened = Archive::open(std::move(*encoded), Format::Jmod);
  ASSERT_TRUE(opened) << opened.error().detail;
  EXPECT_TRUE(opened->find("lib/")->directory);
  EXPECT_EQ(opened->read("lib/native"), bytes("library"));
  EXPECT_TRUE(opened->write());
}

TEST(Archive, PreservesEntryAndArchiveMetadataAndBothOrders) {
  const auto fixture = metadata_fixture();
  const auto baseline = ZipArchive::open(fixture, SourceKind::Jar, {});
  ASSERT_TRUE(baseline) << baseline.error().detail;
  auto jar = Archive::open(fixture, Format::Jar);
  ASSERT_TRUE(jar) << jar.error().detail;
  ASSERT_TRUE(jar->replace("b", bytes("updated")));
  ASSERT_TRUE(jar->add("new", bytes("new"), {{}, 0750}));
  // The directory lists b before a, while the physical stream starts with a.
  auto output = jar->write();
  ASSERT_TRUE(output) << output.error().detail;
  auto zip = ZipArchive::open(std::move(*output), SourceKind::Jar, {});
  ASSERT_TRUE(zip) << zip.error().detail;
  ASSERT_EQ(zip->entries().size(), 3u);
  EXPECT_EQ(zip->entries()[0].name, "b");
  EXPECT_EQ(zip->entries()[1].name, "a");
  EXPECT_EQ(zip->entries()[2].name, "new");
  EXPECT_EQ(zip->find("a")->local_offset, 0u);
  EXPECT_EQ(zip->comment(), bytes("z\0PK\x05\x06"sv));
  for (const auto name : {"a", "b"}) {
    const auto before = baseline->metadata(*baseline->find(name));
    const auto after = zip->metadata(*zip->find(name));
    ASSERT_TRUE(before);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->made_by, before->made_by);
    EXPECT_EQ(after->internal_attributes, before->internal_attributes);
    EXPECT_EQ(after->external_attributes, before->external_attributes);
    EXPECT_EQ(after->local_time, before->local_time);
    EXPECT_EQ(after->central_time, before->central_time);
    EXPECT_EQ(after->local_extra, before->local_extra);
    EXPECT_EQ(after->central_extra, before->central_extra);
    EXPECT_EQ(after->comment, bytes("e\0c"sv));
    EXPECT_FALSE(zip->find(name)->is_deflated());
  }
  EXPECT_EQ(zip->read("a", 8), bytes("x"));
  EXPECT_EQ(zip->read("b", 8), bytes("updated"));
  const auto added = zip->metadata(*zip->find("new"));
  ASSERT_TRUE(added);
  EXPECT_EQ(added->external_attributes >> 16, 0100750u);
}

TEST(Archive, ChecksRetainedPayloadsAndCanRemoveAnUnreadableEntry) {
  auto fixture = metadata_fixture();
  fixture[40] ^= std::byte{1}; // first entry's stored payload
  auto jar = Archive::open(std::move(fixture), Format::Jar);
  ASSERT_TRUE(jar) << jar.error().detail;
  EXPECT_EQ(jar->read("a").error().kind, ErrorKind::InvalidArchive);
  auto invalid = jar->write();
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().kind, ErrorKind::InvalidArchive);
  EXPECT_EQ(invalid.error().entry, "a");
  ASSERT_TRUE(jar->erase("a"));
  EXPECT_TRUE(jar->write());
}

TEST(Archive, WritesZip64WithAnEntryCountPastTheClassicLimit) {
  auto jar = Archive::create(Format::Jar);
  for (std::size_t i = 0; i <= UINT16_MAX; ++i)
    ASSERT_TRUE(jar.add_directory("d" + std::to_string(i) + "/"));
  auto output = jar.write();
  ASSERT_TRUE(output) << output.error().detail;
  const auto locator = output->size() - detail::EOCD_SIZE - 20;
  EXPECT_EQ(detail::little_endian<std::uint32_t>(*output, locator), detail::ZIP64_LOCATOR_MAGIC);
  auto opened = Archive::open(std::move(*output), Format::Jar);
  ASSERT_TRUE(opened) << opened.error().detail;
  EXPECT_EQ(opened->entries().size(), 65'536u);
  EXPECT_TRUE(opened->find("d65535/")->directory);
}

TEST(Archive, RequiresExplicitRemovalOfAllTopLevelSignatureFiles) {
  auto unsigned_input = Archive::create(Format::Jar);
  ASSERT_TRUE(unsigned_input.add("META-INF/MANIFEST.MF", bytes("Manifest-Version: 1.0\r\n\r\n")));
  ASSERT_TRUE(unsigned_input.add("meta-inf/Test.sF", bytes("signature")));
  ASSERT_TRUE(unsigned_input.add("META-INF/sIg-CUSTOM", bytes("block")));
  ASSERT_TRUE(unsigned_input.add("META-INF/dir/Test.RSA", bytes("ordinary resource")));
  auto output = unsigned_input.write();
  ASSERT_TRUE(output) << output.error().detail;
  auto signed_input = Archive::open(std::move(*output), Format::Jar);
  ASSERT_TRUE(signed_input);
  EXPECT_TRUE(signed_input->write());
  ASSERT_TRUE(signed_input->add("new", {}));
  EXPECT_EQ(signed_input->write().error().kind, ErrorKind::SignedJar);
  ASSERT_TRUE(signed_input->erase("meta-inf/Test.sF"));
  EXPECT_EQ(signed_input->write().error().entry, "META-INF/sIg-CUSTOM");
  ASSERT_TRUE(signed_input->erase("META-INF/sIg-CUSTOM"));
  EXPECT_TRUE(signed_input->write());
  EXPECT_EQ(signed_input->read("META-INF/MANIFEST.MF"), bytes("Manifest-Version: 1.0\r\n\r\n"));
  EXPECT_EQ(signed_input->read("META-INF/dir/Test.RSA"), bytes("ordinary resource"));
}

TEST_F(ArchiveTest, FileImportsAreSnapshotsAndSaveReplacesByName) {
  const auto resource = directory / "resource";
  const auto destination = directory / "archive.jar";
  put(resource, "old");
  auto jar = Archive::create(Format::Jar);
  ASSERT_TRUE(jar.add_file("resource", resource));
  put(resource, "new");
  ASSERT_TRUE(jar.save(destination));
  auto snapshot = Archive::open(destination, Format::Jar);
  ASSERT_TRUE(snapshot);
  auto editor = Archive::open(destination, Format::Jar);
  ASSERT_TRUE(editor);
  ASSERT_TRUE(editor->replace_file("resource", resource));
  EXPECT_EQ(editor->add_file("invalid", directory).error().kind, ErrorKind::IoError);
  EXPECT_EQ(editor->save(destination).error().kind, ErrorKind::DestinationExists);
  ASSERT_TRUE(editor->save(destination, SaveMode::ReplaceExisting));
  EXPECT_EQ(snapshot->read("resource"), bytes("old"));
  auto fresh = Archive::open(destination, Format::Jar);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(fresh->read("resource"), bytes("new"));
  EXPECT_EQ(editor->read("resource"), bytes("new"));
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 2);
}

TEST_F(ArchiveTest, BudgetAndCancellationFailuresLeaveOutputAndEditsIntact) {
  Limits limits;
  limits.max_staged_bytes = 5;
  limits.max_archive_bytes = 64;
  limits.max_entry_compressed_bytes = 5;
  limits.max_name_bytes = 3;
  auto jar = Archive::create(Format::Jar, limits);
  EXPECT_EQ(jar.add("long", {}).error().kind, ErrorKind::LimitExceeded);
  ASSERT_TRUE(jar.add("abc", bytes("12345")));
  EXPECT_EQ(jar.add("d", bytes("6")).error().kind, ErrorKind::LimitExceeded);
  EXPECT_EQ(jar.replace("abc", bytes("123456")).error().kind, ErrorKind::LimitExceeded);
  auto destination = directory / "existing.jar";
  put(destination, "untouched");
  EXPECT_EQ(jar.save(destination, SaveMode::ReplaceExisting).error().kind, ErrorKind::LimitExceeded);
  std::ifstream existing(destination, std::ios::binary);
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>(existing), {}), "untouched");
  EXPECT_EQ(jar.read("abc"), bytes("12345"));
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);

  std::stop_source stop;
  limits = {};
  limits.cancellation = stop.get_token();
  auto cancelled = Archive::create(Format::Jar, limits);
  ASSERT_TRUE(cancelled.add("a", bytes("a")));
  ASSERT_TRUE(stop.request_stop());
  EXPECT_EQ(cancelled.write().error().kind, ErrorKind::Cancelled);
  EXPECT_EQ(cancelled.save(directory / "cancelled.jar").error().kind, ErrorKind::Cancelled);
  EXPECT_FALSE(std::filesystem::exists(directory / "cancelled.jar"));
}

TEST(Archive, ImplicitDeflateFallsBackToStoreWhenItsLimitsAreExceeded) {
  Limits limits;
  limits.max_entry_compressed_bytes = 1;
  auto jar = Archive::create(Format::Jar, limits);
  ASSERT_TRUE(jar.add("a", bytes("x")));
  auto output = jar.write();
  ASSERT_TRUE(output) << output.error().detail;
  const auto zip = ZipArchive::open(std::move(*output), SourceKind::Jar, {});
  ASSERT_TRUE(zip);
  EXPECT_FALSE(zip->find("a")->is_deflated());
  EXPECT_EQ(zip->read("a", 1), bytes("x"));

  ASSERT_TRUE(jar.replace("a", bytes("x"), {Compression::Deflate, {}}));
  EXPECT_EQ(jar.write().error().kind, ErrorKind::LimitExceeded);
}

TEST(Archive, DeflatesAndCopiesPayloadsAcrossMultipleChunks) {
  Bytes payload(200 * 1024);
  std::uint32_t state = 17;
  for (auto& byte : payload) {
    state = 1664525u * state + 1013904223u;
    byte = static_cast<std::byte>(state >> 24);
  }
  auto jar = Archive::create(Format::Jar);
  ASSERT_TRUE(jar.add("large", payload, {Compression::Deflate, {}}));
  auto encoded = jar.write();
  ASSERT_TRUE(encoded) << encoded.error().detail;
  auto opened = Archive::open(*encoded, Format::Jar);
  ASSERT_TRUE(opened) << opened.error().detail;
  EXPECT_EQ(opened->read("large"), payload);
  EXPECT_EQ(opened->write(), *encoded);
}

} // namespace
