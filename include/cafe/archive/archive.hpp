/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup archive
 */

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

/**
 * @brief Archive container format selected explicitly by the caller.
 * @ingroup archive
 */
enum class Format {
  Jar, ///< ZIP-based Java archive.
  Jmod, ///< JMOD header plus ZIP entries in permitted module sections.
};
/**
 * @brief Supported ZIP entry compression methods.
 * @ingroup archive
 */
enum class Compression {
  Store, ///< Uncompressed ZIP payload.
  Deflate, ///< Deflate-compressed ZIP payload.
};
/**
 * @brief Destination existence policy for atomic archive publication.
 * @ingroup archive
 */
enum class SaveMode {
  FailIfExists, ///< Publish only when the destination does not exist.
  ReplaceExisting, ///< Atomically replace the destination pathname.
};

/**
 * @brief Archive indexing, staging, compression, and output limits. Byte counts are explicit;
 * cancellation is checked cooperatively during bounded work.
 * @ingroup archive
 */
struct Limits {
  std::uint64_t max_archive_bytes = 1024ull * 1024 * 1024; ///< Maximum archive size in bytes.
  std::size_t max_archive_entries = 150'000; ///< Maximum directory entries in one archive.
  std::size_t max_archive_index_bytes = std::size_t{128} * 1024 * 1024; ///< Maximum accounted archive index memory in bytes.
  std::size_t max_entry_compressed_bytes = std::size_t{256} * 1024 * 1024; ///< Maximum compressed bytes for one archive entry.
  std::size_t max_entry_uncompressed_bytes = std::size_t{256} * 1024 * 1024; ///< Maximum uncompressed bytes for one archive entry.
  std::size_t max_staged_bytes = std::size_t{256} * 1024 * 1024; ///< Maximum total owned uncompressed bytes staged in an archive editor.
  std::uint64_t max_write_uncompressed_bytes = 4ull * 1024 * 1024 * 1024; ///< Maximum cumulative uncompressed bytes processed during archive emission.
  std::size_t max_compression_ratio = 1024; ///< Maximum permitted uncompressed-to-compressed size ratio.
  std::size_t max_name_bytes = 4096; ///< Maximum encoded class/resource/entry name length in bytes.
  std::size_t max_path_depth = 128; ///< Maximum path components in a resource or entry name.
  std::stop_token cancellation; ///< Cooperative stop token checked during bounded work.
};

/**
 * @brief Metadata for a new or replaced file entry.
 *
 * Omitted compression defaults to Deflate and may fall back to Store when default compression
 * exceeds a limit.
 * @ingroup archive
 */
struct EntryOptions {
  std::optional<Compression> compression; ///< Explicit Store/Deflate choice; empty requests the default compression policy.
  std::optional<std::uint16_t> unix_permissions; ///< Optional low nine Unix mode bits.
};

/**
 * @brief Visible archive entry metadata.
 *
 * Names are case-sensitive relative paths using slash separators; directory names end with a
 * slash.
 * @ingroup archive
 */
struct Entry {
  std::string name; ///< Owned case-sensitive relative entry path with slash separators.
  std::uint64_t uncompressed_size; ///< Uncompressed payload size in bytes.
  bool directory; ///< Whether this is an explicit directory entry.
};

/**
 * @brief Archive editing, validation, resource, and I/O failures.
 * @ingroup archive
 */
enum class ErrorKind {
  InvalidName, ///< Name is malformed, unsafe, or unsupported for the operation.
  InvalidOptions, ///< Entry options contain unsupported values.
  EntryExists, ///< An exact entry name already exists.
  EntryNotFound, ///< The exact requested entry is absent.
  EntryIsDirectory, ///< The operation requires a file entry.
  InvalidArchive, ///< Archive framing, checksum, or contents are invalid.
  UnsupportedFeature, ///< Archive uses an unsupported ZIP feature.
  UnsupportedCompression, ///< Entry compression method is unsupported.
  InvalidJmodLayout, ///< JMOD section layout or required module descriptor is invalid.
  SignedJar, ///< Edited signed JAR still contains top-level signature artifacts.
  LimitExceeded, ///< Configured resource ceiling or format capacity would be exceeded.
  Cancelled, ///< The supplied stop token requested cancellation.
  DestinationExists, ///< FailIfExists publication found an existing destination.
  IoError, ///< Filesystem or stream operation failed.
};

/**
 * @brief Archive operation failure with owned path, entry name, and explanation.
 * @ingroup archive
 */
struct Error {
  ErrorKind kind; ///< Discriminator selecting the interpretation of this record.
  std::filesystem::path path; ///< Owned source or destination filesystem path, when applicable.
  std::string entry; ///< Exact slash-separated archive entry name, when applicable.
  std::string detail; ///< Owned human-readable explanation.
};

/**
 * @brief Move-only JAR/JMOD editor with immediately visible staged edits.
 *
 * File-backed input must remain stable; atomic replacement of its pathname is safe. Mutations
 * invalidate entry views and require external synchronization. Allocations may throw
 * std::bad_alloc. A moved-from editor may only be destroyed or assigned.
 * @ingroup archive
 */
class Archive {
public:
  /**
   * @brief Create an empty JAR or JMOD editor.
   * @param format Explicit Jar or Jmod format.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return A new editor; JMOD layout requirements are checked when writing.
   */
  [[nodiscard]] static Archive create(Format format, Limits limits = {});
  /**
   * @brief Validate and index an archive without eagerly inflating its entries.
   *
   * The vector overload takes ownership. For the path overload, keep file contents stable for the
   * editor lifetime; atomic pathname replacement is safe. Entry names are case-sensitive and
   * slash-separated.
   * @param path Source or destination filesystem path.
   * @param format Explicit Jar or Jmod format.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return An editor, or an I/O, format, feature, resource-limit, or cancellation error.
   */
  [[nodiscard]] static std::expected<Archive, Error> open(
      const std::filesystem::path& path, Format format, Limits limits = {});
  /**
   * @brief Validate and index an archive without eagerly inflating its entries.
   *
   * The vector overload takes ownership. For the path overload, keep file contents stable for the
   * editor lifetime; atomic pathname replacement is safe. Entry names are case-sensitive and
   * slash-separated.
   * @param bytes Owned byte buffer transferred into this object.
   * @param format Explicit Jar or Jmod format.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return An editor, or an I/O, format, feature, resource-limit, or cancellation error.
   */
  [[nodiscard]] static std::expected<Archive, Error> open(
      std::vector<std::byte> bytes, Format format, Limits limits = {});

  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~Archive();
  /**
   * @brief Transfer ownership without relocating the backing storage; existing references follow the
   * destination owner.
   */
  Archive(Archive&&) noexcept;
  /**
   * @brief Replace owned state by move assignment; views of the previous destination state are
   * invalidated.
   * @return This object after ownership transfer.
   */
  Archive& operator=(Archive&&) noexcept;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Archive(const Archive&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Archive& operator=(const Archive&) = delete;

  /**
   * @brief Read the selected archive format.
   * @return Jar or Jmod; detection is explicit at open/create time.
   */
  [[nodiscard]] Format format() const;
  /**
   * @brief Inspect the current ordered archive directory, including staged edits.
   * @return A borrowed span invalidated by the next edit or owner destruction.
   */
  [[nodiscard]] std::span<const Entry> entries() const;
  /**
   * @brief Find an exact case-sensitive entry name.
   * @param name Case-sensitive archive entry name with slash separators.
   * @return A borrowed entry pointer, or nullptr if absent; edits invalidate the pointer.
   */
  [[nodiscard]] const Entry* find(std::string_view name) const;
  /**
   * @brief Read the current uncompressed entry contents.
   * @param name Case-sensitive archive entry name with slash separators.
   * @return Owned bytes, or an entry, corruption, decompression-limit, I/O, or cancellation error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(std::string_view name) const;

  /**
   * @brief Stage a new file entry by copying its bytes.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param bytes Borrowed payload bytes copied during this call.
   * @param options Optional compression and Unix permissions for this entry.
   * @return Success or a name/options/existence/budget error; failed staging leaves the editor
   *   unchanged.
   */
  [[nodiscard]] std::expected<void, Error> add(
      std::string_view name, std::span<const std::byte> bytes, EntryOptions options = {});
  /**
   * @brief Stage replacement bytes for an existing file entry.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param bytes Borrowed payload bytes copied during this call.
   * @param options Optional compression and Unix permissions for this entry.
   * @return Success or a missing-entry/directory/options/budget error; failed staging leaves the
   *   editor unchanged.
   */
  [[nodiscard]] std::expected<void, Error> replace(
      std::string_view name, std::span<const std::byte> bytes, EntryOptions options = {});
  /**
   * @brief Snapshot a regular local file into a new archive entry.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param path Source or destination filesystem path.
   * @param options Optional compression and Unix permissions for this entry.
   * @return Success or an input/staging error; later local-file changes do not affect the staged
   *   bytes.
   */
  [[nodiscard]] std::expected<void, Error> add_file(
      std::string_view name, const std::filesystem::path& path, EntryOptions options = {});
  /**
   * @brief Snapshot a regular local file into an existing archive entry.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param path Source or destination filesystem path.
   * @param options Optional compression and Unix permissions for this entry.
   * @return Success or an input/staging error; later local-file changes do not affect the staged
   *   bytes.
   */
  [[nodiscard]] std::expected<void, Error> replace_file(
      std::string_view name, const std::filesystem::path& path, EntryOptions options = {});
  /**
   * @brief Stage an explicit empty directory entry.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param unix_permissions Optional low nine Unix permission bits; values above 0777 are rejected.
   * @return Success or a name/options/existence/budget error; name must end with a slash.
   */
  [[nodiscard]] std::expected<void, Error> add_directory(
      std::string_view name, std::optional<std::uint16_t> unix_permissions = {});
  /**
   * @brief Remove one exact entry without recursively removing descendants.
   * @param name Case-sensitive archive entry name with slash separators.
   * @return Success or EntryNotFound/Cancelled; later table entries shift down.
   */
  [[nodiscard]] std::expected<void, Error> erase(std::string_view name);

  /**
   * @brief Serialize the current archive to owned memory.
   *
   * Retained payloads and supported metadata are preserved; ZIP headers are regenerated. Editing a
   * signed JAR requires explicitly erasing its top-level signature files. A JMOD requires
   * classes/module-info.class and valid section names.
   * @return Complete archive bytes, or a validation/resource/compression error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, Error> write() const;
  /**
   * @brief Write a completed temporary archive and atomically publish it at the destination.
   *
   * FailIfExists is the default. ReplaceExisting atomically replaces an existing destination,
   * including the source pathname. The editor continues to reference its original opened input plus
   * staged edits.
   * @param path Source or destination filesystem path.
   * @param mode Destination-existence policy; FailIfExists is the default.
   * @return Success or a validation/I/O/destination/cancellation error; failures before publication
   *   preserve the destination.
   */
  [[nodiscard]] std::expected<void, Error> save(
      const std::filesystem::path& path, SaveMode mode = SaveMode::FailIfExists) const;

private:
  struct State;
  /**
   * @brief Take ownership of initialized archive editor state.
   * @param state Owned implementation state transferred into this object.
   */
  explicit Archive(std::unique_ptr<State> state);
  std::unique_ptr<State> state; ///< Uniquely owned archive index and staged-edit state.
};

} // namespace cafe::archive
