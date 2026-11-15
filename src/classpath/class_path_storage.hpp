#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>

#include <cafe/classpath/class_path.hpp>

#include "archive/zip_archive.hpp"
#include "classpath_detail.hpp"

namespace cafe::classpath::detail {

struct DirectorySource {
  struct Impl;
  std::filesystem::path root;
  std::shared_ptr<const Impl> handle;
};

struct DirectoryClass {
  struct File;
  SourceProvenance source;
  std::shared_ptr<File> file;

  [[nodiscard]] static std::expected<std::optional<DirectoryClass>, ResolutionError> find(
      const DirectorySource& directory, std::string_view resource);
  [[nodiscard]] std::expected<std::vector<std::byte>, ResolutionError> read(const ClassPathLimits& limits);
};

struct VersionedClass {
  std::uint16_t release;
  const archive::ZipEntry* entry;
};

struct ArchiveSource {
  SourceKind kind;
  std::filesystem::path path;
  archive::ZipArchive zip;
  std::unordered_map<std::string_view, std::vector<VersionedClass>, NameHash, std::equal_to<>> versions;
  std::size_t index_bytes;

  [[nodiscard]] std::optional<SourceProvenance> find(std::string_view resource, std::uint16_t target_release) const;
};

using ExternalSource = std::variant<DirectorySource, std::shared_ptr<const ArchiveSource>>;

struct MemoryClass {
  std::shared_ptr<const ClassInfo> info;
  DeclaredMembers members;
};

[[nodiscard]] std::expected<bool, ResolutionError> parse_multi_release_manifest(
    std::span<const std::byte> bytes, const SourceProvenance& source = {});

} // namespace cafe::classpath::detail

namespace cafe::classpath {

struct ClassPath::Data {
  ClassPathLimits limits;
  DuplicatePolicy duplicates = DuplicatePolicy::FirstWins;
  bool collect_shadows = false;
  std::uint16_t target_release = 0;
  std::unordered_map<std::string, std::vector<std::shared_ptr<const detail::MemoryClass>>, detail::NameHash, std::equal_to<>> memory;
  std::size_t memory_classes = 0;
  std::size_t memory_bytes = 0;
  std::vector<detail::ExternalSource> application;
  std::vector<detail::ExternalSource> runtime;
  std::size_t archive_count = 0;
  std::size_t archive_entries = 0;
  std::size_t archive_index_bytes = 0;
};

} // namespace cafe::classpath
