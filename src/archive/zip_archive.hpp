#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cafe/classpath/class_path.hpp>

namespace cafe::archive {

struct ZipEntry {
  std::string name;
  std::int64_t directory_offset;
  std::uint64_t local_offset;
  std::uint64_t compressed_size;
  std::uint64_t uncompressed_size;
  std::uint32_t crc;
  std::uint16_t method;
  std::uint16_t flags;
  bool directory;

  [[nodiscard]] bool is_deflated() const;
};

struct ZipMetadata {
  std::uint16_t made_by;
  std::uint16_t internal_attributes;
  std::uint32_t external_attributes;
  std::uint32_t local_time;
  std::uint32_t central_time;
  std::uint64_t data_offset;
  std::vector<std::byte> local_extra;
  std::vector<std::byte> central_extra;
  std::vector<std::byte> comment;
};

using ByteSink = std::function<bool(std::span<const std::byte>)>;

class ZipArchive {
public:
  [[nodiscard]] static std::expected<ZipArchive, classpath::ResolutionError> open(
      const std::filesystem::path& path, classpath::SourceKind kind, const classpath::ClassPathLimits& limits);
  // In-memory input uses the same bounded indexing and decoding as files.
  [[nodiscard]] static std::expected<ZipArchive, classpath::ResolutionError> open(
      std::vector<std::byte> bytes, classpath::SourceKind kind, const classpath::ClassPathLimits& limits);

  [[nodiscard]] std::span<const ZipEntry> entries() const;
  [[nodiscard]] const ZipEntry* find(std::string_view name) const;
  [[nodiscard]] std::size_t index_size() const;
  [[nodiscard]] std::expected<std::vector<std::byte>, classpath::ResolutionError> read(
      std::string_view name, std::size_t byte_limit) const;
  [[nodiscard]] std::expected<void, classpath::ResolutionError> verify(const ZipEntry& entry) const;
  [[nodiscard]] std::expected<ZipMetadata, classpath::ResolutionError> metadata(const ZipEntry& entry) const;
  [[nodiscard]] std::expected<std::vector<std::byte>, classpath::ResolutionError> comment() const;
  [[nodiscard]] std::expected<bool, classpath::ResolutionError> copy_compressed(
      const ZipEntry& entry, std::uint64_t offset, const ByteSink& sink) const;

private:
  struct State;
  explicit ZipArchive(std::shared_ptr<State> state);
  std::shared_ptr<State> state;
};

} // namespace cafe::archive
