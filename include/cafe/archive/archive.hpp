#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace cafe::archive {

enum class Format { Jar,
                    Jmod };
enum class Compression { Store,
                         Deflate };
enum class SaveMode { FailIfExists,
                      ReplaceExisting };

struct Limits {
  std::uint64_t max_archive_bytes = 1024ull * 1024 * 1024;
  std::size_t max_archive_entries = 150'000;
  std::size_t max_archive_index_bytes = 128 * 1024 * 1024;
  std::size_t max_entry_compressed_bytes = 256 * 1024 * 1024;
  std::size_t max_entry_uncompressed_bytes = 256 * 1024 * 1024;
  std::size_t max_staged_bytes = 256 * 1024 * 1024;
  std::uint64_t max_write_uncompressed_bytes = 4ull * 1024 * 1024 * 1024;
  std::size_t max_compression_ratio = 1024;
  std::size_t max_name_bytes = 4096;
  std::size_t max_path_depth = 128;
  std::stop_token cancellation;
};

struct EntryOptions {
  std::optional<Compression> compression;
  std::optional<std::uint16_t> unix_permissions;
};

struct Entry {
  std::string name;
  std::uint64_t uncompressed_size;
  bool directory;
};

enum class ErrorKind {
  InvalidName,
  InvalidOptions,
  EntryExists,
  EntryNotFound,
  EntryIsDirectory,
  InvalidArchive,
  UnsupportedFeature,
  UnsupportedCompression,
  InvalidJmodLayout,
  SignedJar,
  LimitExceeded,
  Cancelled,
  DestinationExists,
  IoError,
};

struct Error {
  ErrorKind kind;
  std::filesystem::path path;
  std::string entry;
  std::string detail;
};

class Archive {
public:
  [[nodiscard]] static Archive create(Format format, Limits limits = {});
  // Keep the opened file's contents stable. Atomic replacement of its name is safe.
  [[nodiscard]] static std::expected<Archive, Error> open(
      const std::filesystem::path& path, Format format, Limits limits = {});
  [[nodiscard]] static std::expected<Archive, Error> open(
      std::vector<std::byte> bytes, Format format, Limits limits = {});

  ~Archive();
  Archive(Archive&&) noexcept;
  Archive& operator=(Archive&&) noexcept;
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  [[nodiscard]] Format format() const;
  // Entry views are borrowed until the next edit.
  [[nodiscard]] std::span<const Entry> entries() const;
  [[nodiscard]] const Entry* find(std::string_view name) const;
  [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(std::string_view name) const;

  [[nodiscard]] std::expected<void, Error> add(
      std::string_view name, std::span<const std::byte> bytes, EntryOptions options = {});
  [[nodiscard]] std::expected<void, Error> replace(
      std::string_view name, std::span<const std::byte> bytes, EntryOptions options = {});
  [[nodiscard]] std::expected<void, Error> add_file(
      std::string_view name, const std::filesystem::path& path, EntryOptions options = {});
  [[nodiscard]] std::expected<void, Error> replace_file(
      std::string_view name, const std::filesystem::path& path, EntryOptions options = {});
  [[nodiscard]] std::expected<void, Error> add_directory(
      std::string_view name, std::optional<std::uint16_t> unix_permissions = {});
  [[nodiscard]] std::expected<void, Error> erase(std::string_view name);

  [[nodiscard]] std::expected<std::vector<std::byte>, Error> write() const;
  [[nodiscard]] std::expected<void, Error> save(
      const std::filesystem::path& path, SaveMode mode = SaveMode::FailIfExists) const;

private:
  struct State;
  explicit Archive(std::unique_ptr<State> state);
  std::unique_ptr<State> state;
};

} // namespace cafe::archive
