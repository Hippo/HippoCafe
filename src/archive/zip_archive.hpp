/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

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

/**
 * @brief Validated central-directory entry with a bounded physical payload location. Names are
 * owned by the archive index.
 * @ingroup implementation
 */
struct ZipEntry {
  std::string name; ///< Exact slash-separated archive entry name.
  std::int64_t directory_offset; ///< Logical byte offset of this central-directory record.
  std::uint64_t local_offset; ///< Logical byte offset of this entry's local header.
  std::uint64_t compressed_size; ///< Compressed payload size in bytes.
  std::uint64_t uncompressed_size; ///< Uncompressed payload size in bytes.
  std::uint32_t crc; ///< Expected or computed CRC-32 of the uncompressed payload.
  std::uint16_t method; ///< ZIP compression method number (Store or Deflate).
  std::uint16_t flags; ///< ZIP general-purpose flag bits for the output record.
  bool directory; ///< Whether this is an explicit directory entry.

  /**
   * @brief Check the entry compression method.
   * @return True for Deflate, false for stored entries.
   */
  [[nodiscard]] bool is_deflated() const;
};

/**
 * @brief Retained local/central header metadata and supported extra-field bytes for regenerated
 * output headers.
 * @ingroup implementation
 */
struct ZipMetadata {
  std::uint16_t made_by; ///< ZIP version-made-by field retaining the original host and version.
  std::uint16_t internal_attributes; ///< Original ZIP internal file-attribute bits.
  std::uint32_t external_attributes; ///< Original ZIP external file-attribute bits, including Unix mode when applicable.
  std::uint32_t local_time; ///< Original packed DOS date/time from the local header.
  std::uint32_t central_time; ///< Original packed DOS date/time from the central header.
  std::uint64_t data_offset; ///< Byte offset of the compressed payload in the logical ZIP stream.
  std::vector<std::byte> local_extra; ///< Retained validated local-header extra-field bytes.
  std::vector<std::byte> central_extra; ///< Retained validated central-header extra-field bytes.
  std::vector<std::byte> comment; ///< Retained raw archive or entry comment bytes.
};

/**
 * @brief Callback consuming a byte span; false requests emission to stop.
 */
using ByteSink = std::function<bool(std::span<const std::byte>)>;

/**
 * @brief Shared immutable archive index with serialized access to its pinned input and minizip
 * handle. Reads validate lengths/CRC and enforce decompression budgets; no extraction to disk is
 * required.
 * @ingroup implementation
 */
class ZipArchive {
public:
  /**
   * @brief Validate and index a pinned file or owned in-memory ZIP/JMOD input.
   * @param path Source or destination filesystem path.
   * @param kind Payload or error discriminator.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return A shared archive owner, or a bounded archive/I/O/resource error.
   */
  [[nodiscard]] static std::expected<ZipArchive, classpath::ResolutionError> open(
      const std::filesystem::path& path, classpath::SourceKind kind, classpath::ClassPathLimits limits);
  /**
   * @brief Validate and index a pinned file or owned in-memory ZIP/JMOD input.
   * @param bytes Archive buffer transferred into the returned reader.
   * @param kind Payload or error discriminator.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return A shared archive owner, or a bounded archive/I/O/resource error.
   *
   * In-memory input uses the same bounded indexing and decoding as files.
   */
  [[nodiscard]] static std::expected<ZipArchive, classpath::ResolutionError> open(
      std::vector<std::byte> bytes, classpath::SourceKind kind, classpath::ClassPathLimits limits);

  /**
   * @brief Borrow validated central-directory entries.
   * @return A span valid while shared archive state remains alive.
   */
  [[nodiscard]] std::span<const ZipEntry> entries() const;
  /**
   * @brief Find an exact indexed entry name.
   * @param name Case-sensitive archive entry name with slash separators.
   * @return A borrowed entry pointer, or nullptr when absent.
   */
  [[nodiscard]] const ZipEntry* find(std::string_view name) const;
  /**
   * @brief Report accounted archive index memory.
   * @return Index bytes charged during source registration.
   */
  [[nodiscard]] std::size_t index_size() const;
  /**
   * @brief Decompress and verify one selected file entry under the supplied byte ceiling.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param byte_limit Maximum uncompressed bytes permitted for this read.
   * @return Owned uncompressed bytes, or a lookup/integrity/resource/cancellation error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, classpath::ResolutionError> read(
      std::string_view name, std::size_t byte_limit) const;
  /**
   * @brief Stream and check an entry's CRC/length without retaining decompressed bytes.
   * @param entry Validated central-directory entry belonging to the pinned archive.
   * @return Success or a payload-integrity/resource/cancellation error.
   */
  [[nodiscard]] std::expected<void, classpath::ResolutionError> verify(const ZipEntry& entry) const;
  /**
   * @brief Read validated local and central metadata for an indexed entry.
   * @param entry Validated central-directory entry belonging to the pinned archive.
   * @return Owned retained metadata, or an archive/I/O error.
   */
  [[nodiscard]] std::expected<ZipMetadata, classpath::ResolutionError> metadata(const ZipEntry& entry) const;
  /**
   * @brief Read the original archive comment as raw bytes.
   * @return Owned comment bytes, or an I/O error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, classpath::ResolutionError> comment() const;
  /**
   * @brief Stream a validated original compressed payload to a sink.
   * @param entry Validated central-directory entry belonging to the pinned archive.
   * @param offset Byte offset into the supplied source/output range.
   * @param sink Output callback; false stops emission without publishing partial output.
   * @return Whether output completed, or a source/integrity error; sink refusal is reported without
   *   publishing output.
   */
  [[nodiscard]] std::expected<bool, classpath::ResolutionError> copy_compressed(
      const ZipEntry& entry, std::uint64_t offset, const ByteSink& sink) const;

private:
  struct State;
  /**
   * @brief Initialize shared immutable archive index with serialized access to its pinned input and
   * minizip handle
   * @param state Owned implementation state transferred into this object.
   */
  explicit ZipArchive(std::shared_ptr<State> state);
  std::shared_ptr<State> state; ///< Shared immutable index and pinned input with synchronized stream access.
};

} // namespace cafe::archive
