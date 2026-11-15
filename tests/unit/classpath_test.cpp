#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>
#include <cafe/classpath/class_hierarchy.hpp>

namespace {
using namespace cafe;
using namespace cafe::classpath;

using Bytes = std::vector<std::byte>;

void little_endian(Bytes& out, std::uint64_t value, unsigned width) {
  for (unsigned i = 0; i < width; ++i)
    out.push_back(static_cast<std::byte>(value >> (8 * i)));
}

void big_endian(Bytes& out, std::uint64_t value, unsigned width) {
  for (unsigned i = width; i > 0; --i)
    out.push_back(static_cast<std::byte>(value >> (8 * (i - 1))));
}

void append_text(Bytes& out, std::string_view text) {
  const auto bytes = std::as_bytes(std::span(text));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

Bytes text_bytes(std::string_view text) {
  Bytes result;
  append_text(result, text);
  return result;
}

std::uint32_t zip_crc(const Bytes& data) {
  std::uint32_t crc = UINT32_MAX;
  for (const auto byte : data) {
    crc ^= std::to_integer<std::uint32_t>(byte);
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  return ~crc;
}

Bytes class_bytes(std::string_view name, std::string_view parent, unsigned major = 52) {
  Bytes bytes;
  const auto u2 = [&](unsigned value) { big_endian(bytes, value, 2); };
  const auto add_name = [&](std::string_view text, unsigned index) {
    bytes.push_back(std::byte{1});
    u2(static_cast<unsigned>(text.size()));
    append_text(bytes, text);
    bytes.push_back(std::byte{7});
    u2(index);
  };
  big_endian(bytes, CLASS_MAGIC, 4);
  u2(0);
  u2(major);
  u2(parent.empty() ? 3 : 5);
  add_name(name, 1);
  if (!parent.empty())
    add_name(parent, 3);
  u2(0x0021);
  u2(2);
  u2(parent.empty() ? 0 : 4);
  for (int i = 0; i < 4; ++i)
    u2(0);
  return bytes;
}

struct ZipRecord {
  std::string_view name;
  Bytes content;
  std::uint16_t method = 0;
  bool descriptor = false;
  std::uint32_t attributes = 0;
};

Bytes zip_bytes(std::initializer_list<ZipRecord> records, bool jmod = false, bool wide = false) {
  Bytes body;
  Bytes central;
  const auto local_value = [&](std::uint64_t value, unsigned width) { little_endian(body, value, width); };
  const auto entry_value = [&](std::uint64_t value, unsigned width) { little_endian(central, value, width); };
  for (const auto& record : records) {
    const auto offset = body.size();
    const auto crc = zip_crc(record.content);
    const auto flags = 0x0800u | (record.descriptor ? 8u : 0u);
    const auto size = record.content.size();
    Bytes compressed;
    if (record.method == 8) {
      // Raw DEFLATE with one final stored block. This also gives the test
      // exact control over compressed lengths without a ZIP writer dependency.
      compressed.push_back(std::byte{1});
      little_endian(compressed, size, 2);
      little_endian(compressed, ~size, 2);
    }
    compressed.insert(compressed.end(), record.content.begin(), record.content.end());

    local_value(0x04034b50, 4);
    local_value(wide ? 45 : 20, 2);
    local_value(flags, 2);
    local_value(record.method, 2);
    local_value(0, 4);
    local_value(record.descriptor ? 0 : crc, 4);
    local_value(wide ? UINT32_MAX : record.descriptor ? 0
                                                      : compressed.size(),
                4);
    local_value(wide ? UINT32_MAX : record.descriptor ? 0
                                                      : size,
                4);
    local_value(record.name.size(), 2);
    local_value(wide ? 20 : 0, 2);
    append_text(body, record.name);
    if (wide) {
      local_value(1, 2);
      local_value(16, 2);
      local_value(size, 8);
      local_value(compressed.size(), 8);
    }
    body.insert(body.end(), compressed.begin(), compressed.end());
    if (record.descriptor) {
      local_value(0x08074b50, 4);
      local_value(crc, 4);
      local_value(compressed.size(), wide ? 8 : 4);
      local_value(size, wide ? 8 : 4);
    }

    entry_value(0x02014b50, 4);
    entry_value(0x0314, 2);
    entry_value(wide ? 45 : 20, 2);
    entry_value(flags, 2);
    entry_value(record.method, 2);
    entry_value(0, 4);
    entry_value(crc, 4);
    entry_value(wide ? UINT32_MAX : compressed.size(), 4);
    entry_value(wide ? UINT32_MAX : size, 4);
    entry_value(record.name.size(), 2);
    entry_value(wide ? 28 : 0, 2);
    entry_value(0, 2);
    entry_value(0, 2);
    entry_value(0, 2);
    entry_value(record.attributes, 4);
    entry_value(wide ? UINT32_MAX : offset, 4);
    append_text(central, record.name);
    if (wide) {
      entry_value(1, 2);
      entry_value(24, 2);
      entry_value(size, 8);
      entry_value(compressed.size(), 8);
      entry_value(offset, 8);
    }
  }

  const auto directory_offset = body.size();
  body.insert(body.end(), central.begin(), central.end());
  if (wide) {
    const auto eocd = body.size();
    local_value(0x06064b50, 4);
    local_value(44, 8);
    local_value(45, 2);
    local_value(45, 2);
    local_value(0, 8);
    local_value(records.size(), 8);
    local_value(records.size(), 8);
    local_value(central.size(), 8);
    local_value(directory_offset, 8);
    local_value(0x07064b50, 4);
    local_value(0, 4);
    local_value(eocd, 8);
    local_value(1, 4);
  }
  local_value(0x06054b50, 4);
  local_value(0, 4);
  local_value(wide ? UINT16_MAX : records.size(), 2);
  local_value(wide ? UINT16_MAX : records.size(), 2);
  local_value(wide ? UINT32_MAX : central.size(), 4);
  local_value(wide ? UINT32_MAX : directory_offset, 4);
  local_value(0, 2);
  if (jmod)
    body.insert(body.begin(), {std::byte{'J'}, std::byte{'M'}, std::byte{1}, std::byte{0}});
  return body;
}

class ClassPathTest : public testing::Test {
protected:
  void SetUp() override {
    static std::atomic_uint next = 0;
    const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
    temp = std::filesystem::temp_directory_path() /
           ("hippocafe-classpath-" + std::to_string(clock) + '-' + std::to_string(next.fetch_add(1)));
    ASSERT_TRUE(std::filesystem::create_directory(temp));
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(temp, error);
  }

  std::filesystem::path write(std::filesystem::path name, const Bytes& data) const {
    const auto file = temp / name;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    EXPECT_TRUE(stream.good()) << file;
    return file;
  }

  void add(ClassPathBuilder& path, std::string_view name, std::optional<std::string_view> super,
           std::initializer_list<std::string_view> interfaces = {}, std::uint16_t flags = 0x0021) {
    auto file = ClassFile::create({61, 0}, name, super, flags);
    ASSERT_TRUE(file) << name;
    for (const auto interface : interfaces) {
      auto& pool = file->constant_pool();
      const auto name_index = pool.append_utf8(interface);
      ASSERT_TRUE(name_index);
      const auto index = pool.append(CpEntry{CpTag::Class, 0, *name_index, {}});
      ASSERT_TRUE(index);
      ASSERT_TRUE(file->add_interface(*index));
    }
    const auto added = path.add_class(*file);
    ASSERT_TRUE(added) << added.error().detail;
  }

  std::filesystem::path temp;
};

TEST_F(ClassPathTest, StoredAndDeflatedClassesAreLazyAndRetainProvenance) {
  const auto jar = write("libs.jar", zip_bytes({
                                         {"p/A.class", class_bytes("p/A", "java/lang/Object")},
                                         {"p/B.class", class_bytes("p/B", "p/A"), 8, true},
                                         {"p/Broken.class", text_bytes("bad"), 8},
                                     }));
  ClassPathBuilder builder;
  const auto added = builder.add_jar(jar);
  ASSERT_TRUE(added) << added.error().detail;
  ClassResolver resolver(builder.build());
  EXPECT_EQ(resolver.stats().archive_entries_read, 0u);
  EXPECT_EQ(resolver.stats().metadata_scans, 0u);
  const auto a = resolver.resolve("p/A");
  const auto b = resolver.resolve("p/B");
  ASSERT_TRUE(a) << a.error().detail;
  ASSERT_TRUE(b) << b.error().detail;
  EXPECT_EQ((*a)->name, "p/A");
  EXPECT_EQ((*b)->source.path, jar);
  EXPECT_EQ((*b)->source.entry, "p/B.class");
  EXPECT_EQ((*b)->super_name, "p/A");
  EXPECT_EQ(resolver.stats().archive_entries_read, 2u);
  EXPECT_EQ(resolver.stats().archive_entries_inflated, 1u);
  const auto again = resolver.resolve("p/B");
  ASSERT_TRUE(again);
  EXPECT_EQ(*again, *b);
  EXPECT_EQ(resolver.stats().cache_hits, 1u);
  EXPECT_EQ(resolver.stats().metadata_scans, 2u);
  const auto malformed = resolver.resolve("p/Broken");
  ASSERT_FALSE(malformed);
  EXPECT_EQ(malformed.error().kind, ResolutionErrorKind::MalformedClassMetadata);
  const auto absent = resolver.resolve("p/Unknown");
  ASSERT_FALSE(absent);
  EXPECT_EQ(absent.error().kind, ResolutionErrorKind::NotFound);
  EXPECT_EQ(absent.error().search_path, std::vector{jar});
}

TEST_F(ClassPathTest, JmodChecksHeaderAndIndexesClassesSectionOnly) {
  auto bytes = zip_bytes({
                             {"classes/java/lang/Object.class", class_bytes("java/lang/Object", ""), 8, true},
                             {"classes/module-info.class", text_bytes("ignored")},
                             {"conf/Stray.class", class_bytes("Stray", "java/lang/Object")},
                         },
                         true);
  const auto jmod = write("module.jmod", bytes);
  ClassPathBuilder builder;
  const auto added = builder.add_jmod(jmod);
  ASSERT_TRUE(added) << added.error().detail;
  ClassResolver resolver(builder.build());
  const auto object = resolver.resolve("java/lang/Object");
  ASSERT_TRUE(object) << object.error().detail;
  EXPECT_EQ((*object)->source.kind, SourceKind::Jmod);
  EXPECT_EQ((*object)->source.entry, "classes/java/lang/Object.class");
  const auto absent = resolver.resolve("Stray");
  ASSERT_FALSE(absent);
  EXPECT_EQ(absent.error().kind, ResolutionErrorKind::NotFound);

  bytes[2] = std::byte{2};
  const auto invalid_header = builder.add_jmod(write("future.jmod", bytes));
  ASSERT_FALSE(invalid_header);
  EXPECT_EQ(invalid_header.error().kind, ResolutionErrorKind::UnsupportedArchiveFeature);
  const auto not_a_jmod = builder.add_jmod(write("plain.jmod", zip_bytes({})));
  ASSERT_FALSE(not_a_jmod);
  EXPECT_EQ(not_a_jmod.error().kind, ResolutionErrorKind::ArchiveCorrupt);
}

TEST_F(ClassPathTest, BoundsAndReadsZip64Archive) {
  const auto jar = write("wide.jar", zip_bytes({
                                                   {"p/A.class", class_bytes("p/A", "java/lang/Object"), 8, true},
                                               },
                                               false, true));
  ClassPathBuilder builder;
  const auto added = builder.add_jar(jar);
  ASSERT_TRUE(added) << added.error().detail;
  const auto info = ClassResolver(builder.build()).resolve("p/A");
  ASSERT_TRUE(info) << info.error().detail;
  EXPECT_EQ((*info)->name, "p/A");
}

TEST_F(ClassPathTest, IgnoresFooterSignatureInAnOpaqueZipComment) {
  auto bytes = zip_bytes({{"p/A.class", class_bytes("p/A", "java/lang/Object")}});
  constexpr auto COMMENT_SIZE = 28;
  bytes[bytes.size() - 2] = std::byte{COMMENT_SIZE};
  little_endian(bytes, 0x06054b50, 4);
  bytes.resize(bytes.size() + COMMENT_SIZE - 4, std::byte{255});
  const auto jar = write("comment.jar", bytes);
  ClassPathBuilder builder;
  const auto added = builder.add_jar(jar);
  ASSERT_TRUE(added) << added.error().detail;
  EXPECT_TRUE(ClassResolver(builder.build()).resolve("p/A"));
}

TEST_F(ClassPathTest, SelectsMultiReleaseClassByManifestAndTarget) {
  const auto jar = write("multi.jar", zip_bytes({
                                          {"META-INF/versions/11/p/C.class", class_bytes("p/C", "p/Eleven", 55), 8},
                                          {"p/C.class", class_bytes("p/C", "p/Base")},
                                          {"META-INF/versions/9/p/C.class", class_bytes("p/C", "p/Nine", 53)},
                                          {"META-INF/versions/8/p/C.class", class_bytes("p/C", "p/Illegal")},
                                          {"META-INF/versions/011/p/C.class", class_bytes("p/C", "p/Illegal", 55)},
                                          {"META-INF/MANIFEST.MF", text_bytes("Manifest-Version: 1.0\r\nmUlTi-ReLeAsE: Tr\r\n uE\r\n\r\n"), 8, true},
                                      }));
  ClassPathBuilder builder;
  const auto added = builder.add_jar(jar);
  ASSERT_TRUE(added) << added.error().detail;
  builder.set_target_release(8);
  ClassResolver baseline(builder.build());
  builder.set_target_release(10);
  ClassResolver intermediate(builder.build());
  builder.set_target_release(17);
  ClassResolver latest(builder.build());
  const auto c8 = baseline.resolve("p/C");
  const auto c10 = intermediate.resolve("p/C");
  const auto c17 = latest.resolve("p/C");
  ASSERT_TRUE(c8) << c8.error().detail;
  ASSERT_TRUE(c10) << c10.error().detail;
  ASSERT_TRUE(c17) << c17.error().detail;
  EXPECT_EQ((*c8)->super_name, "p/Base");
  EXPECT_FALSE((*c8)->source.release);
  EXPECT_EQ((*c10)->super_name, "p/Nine");
  EXPECT_EQ((*c10)->source.release, 9);
  EXPECT_EQ((*c17)->super_name, "p/Eleven");
  EXPECT_EQ((*c17)->source.release, 11);
}

TEST_F(ClassPathTest, IgnoresVersionedEntriesWithoutMainSectionOptIn) {
  const auto jar = write("single.jar", zip_bytes({
                                           {"p/C.class", class_bytes("p/C", "p/Base")},
                                           {"META-INF/versions/17/p/C.class", class_bytes("p/C", "p/Versioned", 61)},
                                           {"META-INF/MANIFEST.MF", text_bytes("Manifest-Version: 1.0\n\nName: p/C.class\nMulti-Release: true\n\n")},
                                       }));
  ClassPathBuilder builder;
  builder.set_target_release(17);
  ASSERT_TRUE(builder.add_jar(jar));
  const auto chosen = ClassResolver(builder.build()).resolve("p/C");
  ASSERT_TRUE(chosen);
  EXPECT_EQ((*chosen)->super_name, "p/Base");

  const auto impossible = write("impossible.jar", zip_bytes({
                                                      {"p/C.class", class_bytes("p/C", "p/Base")},
                                                      {"META-INF/versions/9/p/C.class", class_bytes("p/C", "p/Versioned", 61)},
                                                      {"META-INF/MANIFEST.MF", text_bytes("Manifest-Version: 1.0\nMulti-Release: true\n\n")},
                                                  }));
  ClassPathBuilder second;
  second.set_target_release(17);
  ASSERT_TRUE(second.add_jar(impossible));
  const auto incompatible = ClassResolver(second.build()).resolve("p/C");
  ASSERT_FALSE(incompatible);
  EXPECT_EQ(incompatible.error().kind, ResolutionErrorKind::UnsupportedClassVersion);
}

TEST_F(ClassPathTest, UsesModifiedUtf8ClassNameAndUtf8ResourcePath) {
  const std::string_view model_name = "p/\xed\xa0\x81\xed\xb0\x80";
  const auto jar = write("unicode.jar", zip_bytes({
                                            {"p/\xf0\x90\x90\x80.class", class_bytes(model_name, "java/lang/Object")},
                                        }));
  ClassPathBuilder builder;
  ASSERT_TRUE(builder.add_jar(jar));
  const auto info = ClassResolver(builder.build()).resolve(model_name);
  ASSERT_TRUE(info) << info.error().detail;
  EXPECT_EQ((*info)->name, model_name);
}

TEST_F(ClassPathTest, RespectsSourceOrderStrictDuplicatesAndSnapshots) {
  const auto first = write("first.jar", zip_bytes({{"p/A.class", class_bytes("p/A", "p/First")}}));
  const auto second = write("second.jar", zip_bytes({{"p/A.class", class_bytes("p/A", "p/Second")}}));
  const auto runtime = write("runtime.jar", zip_bytes({{"p/A.class", class_bytes("p/A", "p/Runtime")}}));
  ClassPathBuilder builder;
  ASSERT_TRUE(builder.add_jar(runtime, SourceTier::Runtime));
  ASSERT_TRUE(builder.add_jar(first));
  ASSERT_TRUE(builder.add_jar(second));
  builder.set_duplicate_policy(DuplicatePolicy::FirstWins, true);
  ClassResolver before(builder.build());
  const auto chosen = before.resolve("p/A");
  ASSERT_TRUE(chosen);
  EXPECT_EQ((*chosen)->source.path, first);
  ASSERT_EQ((*chosen)->shadowed_definitions.size(), 2u);
  EXPECT_EQ((*chosen)->shadowed_definitions[0].path, second);
  EXPECT_EQ((*chosen)->shadowed_definitions[1].path, runtime);

  add(builder, "p/A", "p/Memory");
  const auto memory = ClassResolver(builder.build()).resolve("p/A");
  ASSERT_TRUE(memory);
  EXPECT_EQ((*memory)->source.kind, SourceKind::Memory);
  EXPECT_EQ((*memory)->super_name, "p/Memory");
  const auto original = before.resolve("p/A");
  ASSERT_TRUE(original);
  EXPECT_EQ(*original, *chosen);

  builder.set_duplicate_policy(DuplicatePolicy::Reject);
  const auto conflict = ClassResolver(builder.build()).resolve("p/A");
  ASSERT_FALSE(conflict);
  EXPECT_EQ(conflict.error().kind, ResolutionErrorKind::SourceConflict);
  EXPECT_EQ(conflict.error().path, first);
}

TEST_F(ClassPathTest, DirectoryUsesRegularFilesAndRejectsEscapes) {
  write("root/p/A.class", class_bytes("p/A", "java/lang/Object"));
  write("root/p/Wrong.class", class_bytes("p/A", "java/lang/Object"));
  ClassPathBuilder builder;
  ASSERT_TRUE(builder.add_directory(temp / "root"));
  ClassResolver resolver(builder.build());
  const auto a = resolver.resolve("p/A");
  ASSERT_TRUE(a) << a.error().detail;
  EXPECT_EQ((*a)->source.kind, SourceKind::Directory);
  EXPECT_EQ((*a)->source.entry, "p/A.class");
  const auto mismatch = resolver.resolve("p/Wrong");
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().kind, ResolutionErrorKind::ClassNameMismatch);
  for (const auto name : {"p/../A", "/p/A", "p\\A", "p//A", "p/:A", "p/\xc0\x80", "[Lp/A;"}) {
    const auto invalid = resolver.resolve(name);
    ASSERT_FALSE(invalid) << name;
    EXPECT_EQ(invalid.error().kind, ResolutionErrorKind::InvalidName) << name;
  }

  const auto file = write("outside/p/Escape.class", class_bytes("p/Escape", "java/lang/Object"));
  std::error_code error;
  std::filesystem::create_symlink(file, temp / "root/p/Escape.class", error);
  if (error)
    GTEST_SKIP() << "file symlinks are unavailable: " << error.message();
  const auto escaped = resolver.resolve("p/Escape");
  ASSERT_FALSE(escaped);
  EXPECT_EQ(escaped.error().kind, ResolutionErrorKind::UnsafePath);
}

TEST_F(ClassPathTest, DiscoversJmodAndLegacyLayoutsExplicitly) {
  write("jdk/jmods/java.base.jmod", zip_bytes({
                                                  {"classes/java/lang/Object.class", class_bytes("java/lang/Object", "", 65), 8},
                                              },
                                              true));
  write("legacy/jre/lib/rt.jar", zip_bytes({{"java/lang/Object.class", class_bytes("java/lang/Object", "")}}));
  write("legacy/jre/lib/ext/extra.jar", zip_bytes({{"p/Extra.class", class_bytes("p/Extra", "java/lang/Object")}}));
  write("linked/lib/modules", {});
  ClassPathBuilder legacy;
  const auto old = legacy.add_java_home(temp / "legacy");
  ASSERT_TRUE(old) << old.error().detail;
  ClassResolver resolver(legacy.build());
  const auto object = resolver.resolve("java/lang/Object");
  const auto extra = resolver.resolve("p/Extra");
  ASSERT_TRUE(object);
  ASSERT_TRUE(extra);
  EXPECT_EQ((*object)->version.major, 52);
  EXPECT_EQ((*extra)->source.path.filename(), "extra.jar");

  ClassPathBuilder current;
  const auto modular = current.add_java_home(temp / "jdk");
  ASSERT_TRUE(modular) << modular.error().detail;
  const auto other = ClassResolver(current.build()).resolve("java/lang/Object");
  ASSERT_TRUE(other);
  EXPECT_EQ((*other)->version.major, 65);

  const auto linked = current.add_java_home(temp / "linked");
  ASSERT_FALSE(linked);
  EXPECT_EQ(linked.error().kind, ResolutionErrorKind::UnsupportedRuntime);
  EXPECT_NE(linked.error().detail.find("lib/modules"), std::string::npos);
}

TEST_F(ClassPathTest, RejectsMalformedArchivesAndSelectedEntries) {
  const auto clazz = class_bytes("p/A", "java/lang/Object");
  const auto duplicate = write("dup.jar", zip_bytes({{"p/A.class", clazz}, {"p/A.class", clazz}}));
  const auto traversal = write("traversal.jar", zip_bytes({{"../p/A.class", clazz}}));
  auto footer = zip_bytes({{"p/A.class", clazz}});
  footer.pop_back();
  const auto truncated = write("truncated.jar", footer);
  ClassPathBuilder builder;
  for (const auto& path : {duplicate, traversal, truncated}) {
    const auto added = builder.add_jar(path);
    ASSERT_FALSE(added) << path;
    EXPECT_EQ(added.error().kind, ResolutionErrorKind::ArchiveCorrupt) << path;
  }

  auto overlapping = zip_bytes({{"p/A.class", clazz}, {"p/B.class", clazz}});
  // Classic central entries are 46 fixed bytes plus the path and no extras.
  const auto second = overlapping.size() - 22 - 46 - std::string_view("p/B.class").size();
  std::fill_n(overlapping.begin() + static_cast<std::ptrdiff_t>(second + 42), 4, std::byte{0});
  const auto overlap = builder.add_jar(write("overlap.jar", overlapping));
  ASSERT_FALSE(overlap);
  EXPECT_EQ(overlap.error().kind, ResolutionErrorKind::ArchiveCorrupt);

  const auto symlink = builder.add_jar(write("links.jar", zip_bytes({
                                                              {"p/A.class", text_bytes("/outside.class"), 0, false, 0120000u << 16},
                                                          })));
  ASSERT_FALSE(symlink);
  EXPECT_EQ(symlink.error().kind, ResolutionErrorKind::UnsupportedArchiveFeature);

  auto bad_crc = zip_bytes({{"p/A.class", clazz, 8}});
  bad_crc[30 + std::string_view("p/A.class").size() + 5] ^= std::byte{1};
  ASSERT_TRUE(builder.add_jar(write("crc.jar", bad_crc)));
  ASSERT_TRUE(builder.add_jar(write("compression.jar", zip_bytes({{"p/B.class", clazz, 12}}))));
  ClassResolver resolver(builder.build());
  const auto corrupt = resolver.resolve("p/A");
  ASSERT_FALSE(corrupt);
  EXPECT_EQ(corrupt.error().kind, ResolutionErrorKind::ArchiveCorrupt);
  const auto unsupported = resolver.resolve("p/B");
  ASSERT_FALSE(unsupported);
  EXPECT_EQ(unsupported.error().kind, ResolutionErrorKind::UnsupportedCompression);
}

TEST_F(ClassPathTest, AppliesIndexReadMetadataAndCancellationLimits) {
  const auto jar = write("limits.jar", zip_bytes({{"p/A.class", class_bytes("p/A", "java/lang/Object"), 8}}));
  ClassPathLimits small_index;
  small_index.max_archive_entries = 0;
  const auto indexed = ClassPathBuilder(small_index).add_jar(jar);
  ASSERT_FALSE(indexed);
  EXPECT_EQ(indexed.error().kind, ResolutionErrorKind::ResourceLimitExceeded);

  ClassPathLimits small_read;
  small_read.max_resolution_read_bytes = 5;
  ClassPathBuilder small(small_read);
  ASSERT_TRUE(small.add_jar(jar));
  const auto large = ClassResolver(small.build()).resolve("p/A");
  ASSERT_FALSE(large);
  EXPECT_EQ(large.error().kind, ResolutionErrorKind::EntryTooLarge);

  ClassPathLimits small_metadata;
  small_metadata.max_class_metadata_bytes = 16;
  ClassPathBuilder metadata(small_metadata);
  ASSERT_TRUE(metadata.add_jar(jar));
  const auto oversized = ClassResolver(metadata.build()).resolve("p/A");
  ASSERT_FALSE(oversized);
  EXPECT_EQ(oversized.error().kind, ResolutionErrorKind::ResourceLimitExceeded);

  std::stop_source stop;
  ClassPathLimits cancellation;
  cancellation.cancellation = stop.get_token();
  ClassPathBuilder stopped(cancellation);
  ASSERT_TRUE(stopped.add_jar(jar));
  stop.request_stop();
  const auto cancelled = ClassResolver(stopped.build()).resolve("p/A");
  ASSERT_FALSE(cancelled);
  EXPECT_EQ(cancelled.error().kind, ResolutionErrorKind::Cancelled);
}

TEST_F(ClassPathTest, MetadataScannerIgnoresMalformedOptionalFixtures) {
  for (const auto name : {"OldPoisonedAttribute", "ModernPoisonedAttribute", "AttributeOverrun", "LegacySubroutine"}) {
    const auto source = std::filesystem::path(HIPPOCAFE_FIXTURE_DIR) / "generated" / (std::string(name) + ".class");
    std::ifstream file(source, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.good()) << source;
    const auto size = file.tellg();
    ASSERT_GT(size, 0);
    Bytes bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    ASSERT_TRUE(file.good()) << source;
    write(std::filesystem::path("metadata") / source.filename(), bytes);
  }
  ClassPathBuilder builder;
  ASSERT_TRUE(builder.add_directory(temp / "metadata"));
  ClassResolver resolver(builder.build());
  for (const auto name : {"OldPoisonedAttribute", "ModernPoisonedAttribute", "AttributeOverrun", "LegacySubroutine"}) {
    const auto resolved = resolver.resolve(name);
    ASSERT_TRUE(resolved) << resolved.error().detail;
    EXPECT_EQ((*resolved)->name, name);
  }
  EXPECT_EQ(resolver.stats().member_scans, 0u);
  const ClassHierarchy hierarchy(resolver);
  const auto main = hierarchy.declared_member_flags("ModernPoisonedAttribute", "main", "([Ljava/lang/String;)V", true);
  ASSERT_TRUE(main) << main.error().detail;
  ASSERT_TRUE(*main);
  EXPECT_EQ(**main, 0x0009);
  const auto absent = hierarchy.declared_member_flags("ModernPoisonedAttribute", "missing", "()V", true);
  ASSERT_TRUE(absent);
  EXPECT_FALSE(*absent);
  EXPECT_EQ(resolver.stats().member_scans, 1u);
}

TEST_F(ClassPathTest, DeterminesAssignableClassesInterfacesAndArrays) {
  constexpr std::uint16_t INTERFACE = 0x0601;
  ClassPathBuilder builder;
  add(builder, "java/lang/Object", std::nullopt);
  add(builder, "java/lang/Cloneable", "java/lang/Object", {}, INTERFACE);
  add(builder, "java/io/Serializable", "java/lang/Object", {}, INTERFACE);
  add(builder, "p/I", "java/lang/Object", {}, INTERFACE);
  add(builder, "p/J", "java/lang/Object", {"p/I"}, INTERFACE);
  add(builder, "p/K", "java/lang/Object", {"p/I"}, INTERFACE);
  add(builder, "p/Diamond", "java/lang/Object", {"p/J", "p/K"}, INTERFACE);
  add(builder, "p/Base", "java/lang/Object", {"p/Diamond"});
  add(builder, "p/Sub", "p/Base");
  const ClassHierarchy hierarchy(ClassResolver(builder.build()));
  struct Assignment {
    std::string_view source;
    std::string_view target;
    bool expected;
  };
  for (const auto& entry : {
           Assignment{"p/Sub", "p/I", true},
           Assignment{"p/Sub", "p/K", true},
           Assignment{"p/I", "java/lang/Object", true},
           Assignment{"p/Base", "p/Sub", false},
           Assignment{"p/J", "p/K", false},
           Assignment{"[Lp/Sub;", "[Lp/Base;", true},
           Assignment{"[Lp/Sub;", "[Lp/I;", true},
           Assignment{"[B", "[Z", false},
           Assignment{"[I", "[I", true},
           Assignment{"[J", "[D", false},
           Assignment{"[[J", "[Ljava/lang/Object;", true},
           Assignment{"[[J", "[Ljava/lang/Cloneable;", true},
           Assignment{"[[J", "[[Ljava/lang/Object;", false},
           Assignment{"[I", "java/lang/Cloneable", true},
           Assignment{"[I", "java/io/Serializable", true},
           Assignment{"[I", "p/I", false},
       }) {
    const auto assignable = hierarchy.is_assignable(entry.source, entry.target);
    ASSERT_TRUE(assignable) << assignable.error().detail;
    EXPECT_EQ(*assignable, entry.expected) << entry.source << " -> " << entry.target;
  }
}

TEST_F(ClassPathTest, JoinsMostSpecificReferenceTypesAndReportsAmbiguity) {
  constexpr std::uint16_t INTERFACE = 0x0601;
  ClassPathBuilder builder;
  add(builder, "java/lang/Object", std::nullopt);
  add(builder, "p/I", "java/lang/Object", {}, INTERFACE);
  add(builder, "p/J", "java/lang/Object", {}, INTERFACE);
  add(builder, "p/Base", "java/lang/Object", {"p/I"});
  add(builder, "p/A", "p/Base", {"p/J"});
  add(builder, "p/B", "p/Base", {"p/J"});
  add(builder, "p/C", "java/lang/Object", {"p/I"});
  const ClassHierarchy hierarchy(ClassResolver(builder.build()));

  const auto most_specific = hierarchy.join_reference_types("p/A", "p/C");
  ASSERT_TRUE(most_specific) << most_specific.error().detail;
  EXPECT_EQ(most_specific->type, "p/I");
  EXPECT_TRUE(most_specific->ambiguous_candidates.empty());

  const auto ambiguous = hierarchy.join_reference_types("p/A", "p/B");
  ASSERT_TRUE(ambiguous) << ambiguous.error().detail;
  EXPECT_EQ(ambiguous->type, "p/Base");
  EXPECT_EQ(ambiguous->ambiguous_candidates, (std::vector<std::string>{"p/Base", "p/J"}));
  const auto array = hierarchy.join_reference_types("[[Lp/A;", "[[Lp/C;");
  ASSERT_TRUE(array) << array.error().detail;
  EXPECT_EQ(array->type, "[[Lp/I;");
  const auto primitives = hierarchy.join_reference_types("[[I", "[[J");
  ASSERT_TRUE(primitives) << primitives.error().detail;
  EXPECT_EQ(primitives->type, "[Ljava/lang/Object;");
  EXPECT_EQ(primitives->ambiguous_candidates,
            (std::vector<std::string>{"[Ljava/io/Serializable;", "[Ljava/lang/Cloneable;"}));
}

TEST_F(ClassPathTest, MissingAncestorsCyclesAndInvalidParentsAreDistinguished) {
  constexpr std::uint16_t INTERFACE = 0x0601;
  ClassPathBuilder builder;
  add(builder, "java/lang/Object", std::nullopt);
  add(builder, "p/BrokenInterface", "java/lang/Object", {"p/Missing"}, INTERFACE);
  add(builder, "p/Broken", "java/lang/Object", {"p/BrokenInterface"});
  add(builder, "p/CycleA", "p/CycleB");
  add(builder, "p/CycleB", "p/CycleA");
  add(builder, "p/I", "java/lang/Object", {"p/J"}, INTERFACE);
  add(builder, "p/J", "java/lang/Object", {"p/I"}, INTERFACE);
  add(builder, "p/Wrong", "p/I");
  add(builder, "p/Final", "java/lang/Object", {}, 0x0031);
  add(builder, "p/WrongFinal", "p/Final");
  const ClassHierarchy hierarchy(ClassResolver(builder.build()));

  const auto missing = hierarchy.is_assignable("p/Broken", "java/lang/Object");
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().kind, ResolutionErrorKind::NotFound);
  EXPECT_EQ(missing.error().name, "p/Missing");

  for (const auto name : {"p/CycleA", "p/I", "p/Wrong"}) {
    const auto cycles = hierarchy.is_assignable(name, "java/lang/Object");
    ASSERT_FALSE(cycles);
    EXPECT_EQ(cycles.error().kind, ResolutionErrorKind::HierarchyCycle);
    EXPECT_NE(cycles.error().detail.find(" -> "), std::string::npos);
  }
  const auto unsupported = hierarchy.is_assignable("p/WrongFinal", "java/lang/Object");
  ASSERT_FALSE(unsupported);
  EXPECT_EQ(unsupported.error().kind, ResolutionErrorKind::InvalidParent);
}

TEST_F(ClassPathTest, HierarchyOverlayUpdatesOnlyItsOwnSnapshot) {
  constexpr std::uint16_t INTERFACE = 0x0601;
  ClassPathBuilder builder;
  add(builder, "java/lang/Object", std::nullopt);
  add(builder, "p/I", "java/lang/Object", {}, INTERFACE);
  add(builder, "p/Current", "java/lang/Object", {"p/I"});
  const ClassHierarchy original(ClassResolver(builder.build()));
  const auto prior = original.is_assignable("p/Current", "p/I");
  ASSERT_TRUE(prior);
  EXPECT_TRUE(*prior);

  auto modified = ClassFile::create({61, 0}, "p/Current");
  ASSERT_TRUE(modified);
  const auto method = modified->constant_pool().append_utf8("protectedMethod");
  const auto descriptor = modified->constant_pool().append_utf8("()V");
  ASSERT_TRUE(method && descriptor);
  ASSERT_TRUE(modified->add_method(0x0004, *method, *descriptor));
  const auto updated = original.with_class(*modified);
  ASSERT_TRUE(updated) << updated.error().detail;
  EXPECT_NE(original.snapshot_id(), updated->snapshot_id());
  const auto reused = original.with_class(*modified);
  ASSERT_TRUE(reused);
  EXPECT_EQ(reused->snapshot_id(), updated->snapshot_id());
  const auto overlay = updated->is_assignable("p/Current", "p/I");
  ASSERT_TRUE(overlay);
  EXPECT_FALSE(*overlay);

  const auto declared = updated->declared_member_flags("p/Current", "protectedMethod", "()V", true);
  ASSERT_TRUE(declared && *declared);
  EXPECT_EQ(**declared, 0x0004);
  const auto absent = original.declared_member_flags("p/Current", "protectedMethod", "()V", true);
  ASSERT_TRUE(absent);
  EXPECT_FALSE(*absent);

  const auto again = original.is_assignable("p/Current", "p/I");
  ASSERT_TRUE(again);
  EXPECT_TRUE(*again);

  const auto name_index = modified->constant_pool().append_utf8("p/I");
  ASSERT_TRUE(name_index);
  const auto interface = modified->constant_pool().append(CpEntry{CpTag::Class, 0, *name_index, {}});
  ASSERT_TRUE(interface);
  ASSERT_TRUE(modified->add_interface(*interface));
  const auto changed = updated->with_class(*modified);
  ASSERT_TRUE(changed);
  EXPECT_NE(changed->snapshot_id(), updated->snapshot_id());
  const auto now = changed->is_assignable("p/Current", "p/I");
  ASSERT_TRUE(now);
  EXPECT_TRUE(*now);
  const auto old = updated->is_assignable("p/Current", "p/I");
  ASSERT_TRUE(old);
  EXPECT_FALSE(*old);
}

TEST_F(ClassPathTest, HierarchyWorkRespectsDepthAndEdgeLimits) {
  ClassPathLimits limits;
  limits.max_hierarchy_depth = 2;
  ClassPathBuilder path(limits);
  add(path, "java/lang/Object", std::nullopt);
  add(path, "p/B", "java/lang/Object");
  add(path, "p/A", "p/B");
  const auto result = ClassHierarchy(ClassResolver(path.build())).is_assignable("p/A", "java/lang/Object");
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, ResolutionErrorKind::ResourceLimitExceeded);

  constexpr std::uint16_t INTERFACE = 0x0601;
  ClassPathLimits edge_limits;
  edge_limits.max_hierarchy_edges = 3;
  ClassPathBuilder diamond(edge_limits);
  add(diamond, "java/lang/Object", std::nullopt);
  add(diamond, "p/I", "java/lang/Object", {}, INTERFACE);
  add(diamond, "p/J", "java/lang/Object", {"p/I"}, INTERFACE);
  add(diamond, "p/A", "java/lang/Object", {"p/J"}, INTERFACE);
  const auto too_many = ClassHierarchy(ClassResolver(diamond.build())).is_assignable("p/A", "java/lang/Object");
  ASSERT_FALSE(too_many);
  EXPECT_EQ(too_many.error().kind, ResolutionErrorKind::ResourceLimitExceeded);
}

} // namespace
