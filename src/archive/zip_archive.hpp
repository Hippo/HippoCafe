#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
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

private:
  struct State;
  explicit ZipArchive(std::shared_ptr<State> state);
  std::shared_ptr<State> state;
};

} // namespace cafe::archive
