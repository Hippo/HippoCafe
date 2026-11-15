/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup implementation
 */

#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>

#include <cafe/classpath/class_path.hpp>

#include "archive/zip_archive.hpp"
#include "classpath_detail.hpp"

namespace cafe::classpath::detail {

/**
 * @brief Pinned registered directory root and its platform-native handle.
 * @ingroup implementation
 */
struct DirectorySource {
  struct Impl;
  std::filesystem::path root; ///< Registered class-directory root pathname.
  std::shared_ptr<const Impl> handle; ///< Pinned native or minizip handle; ownership follows the field type.
};

/**
 * @brief Selected regular class file pinned during metadata/member scanning, with its source
 * provenance.
 * @ingroup implementation
 */
struct DirectoryClass {
  struct File;
  SourceProvenance source; ///< Provenance of the selected definition.
  std::shared_ptr<File> file; ///< Pinned native file/stream owner retained for safe reads or publication.

  /**
   * @brief Open a class resource beneath a pinned directory root without following descendant
   * symlinks.
   * @param directory Pinned registered class-directory source.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @return A pinned regular file, an empty optional if absent, or a path/I/O failure.
   */
  [[nodiscard]] static std::expected<std::optional<DirectoryClass>, ResolutionError> find(
      const DirectorySource& directory, std::string_view resource);
  /**
   * @brief Read the pinned class file under the configured resolution and class-byte ceilings.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @return Owned bytes, or a bounded I/O/resource/cancellation error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, ResolutionError> read(const ClassPathLimits& limits);
};

/**
 * @brief Multi-release JAR candidate borrowing an entry from its pinned archive index.
 * @ingroup implementation
 */
struct VersionedClass {
  std::uint16_t release; ///< Java release number from a versioned JAR entry, at least 9.
  const archive::ZipEntry* entry; ///< Borrowed entry in the pinned archive index.
};

/**
 * @brief Shared indexed JAR/JMOD source with bounded manifest-enabled multi-release selection
 * data.
 * @ingroup implementation
 */
struct ArchiveSource {
  SourceKind kind; ///< Discriminator selecting the interpretation of this record.
  std::filesystem::path path; ///< Owned filesystem path identifying this source.
  archive::ZipArchive zip; ///< Shared validated JAR/JMOD index and pinned input.
  std::unordered_map<std::string_view, std::vector<VersionedClass>, NameHash, std::equal_to<>> versions; ///< Manifest-enabled versioned class candidates grouped by base resource name.
  std::size_t index_bytes; ///< Accounted archive index storage in bytes.

  /**
   * @brief Select a base or highest eligible manifest-enabled multi-release class entry.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @param target_release Explicit Java release ceiling; zero disables the ceiling and versioned
   *   selection.
   * @return Selected provenance, or nullopt when the resource is absent.
   */
  [[nodiscard]] std::optional<SourceProvenance> find(std::string_view resource, std::uint16_t target_release) const;
};

/**
 * @brief Pinned directory source or shared immutable archive source.
 */
using ExternalSource = std::variant<DirectorySource, std::shared_ptr<const ArchiveSource>>;

/**
 * @brief Immutable copied class and member metadata registered in the in-memory source tier.
 * @ingroup implementation
 */
struct MemoryClass {
  std::shared_ptr<const ClassInfo> info; ///< Shared immutable hierarchy metadata for this definition.
  DeclaredMembers members; ///< Owned or shared declaration tables for exact member lookup.
};

/**
 * @brief Parse bounded manifest main-section headers with continuation-line handling.
 * @param bytes Manifest contents borrowed only for this call.
 * @param source Provenance copied into returned metadata or errors.
 * @return Whether Multi-Release is true, or a malformed-manifest failure.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<bool, ResolutionError> parse_multi_release_manifest(
    std::span<const std::byte> bytes, const SourceProvenance& source = {});

} // namespace cafe::classpath::detail

namespace cafe::classpath {

/**
 * @brief Snapshot registration state, deterministic source tiers, target-release/duplicate policy,
 * and cumulative source-budget accounting.
 * @ingroup implementation
 */
struct ClassPath::Data {
  ClassPathLimits limits; ///< Configured resource ceilings.
  DuplicatePolicy duplicates = DuplicatePolicy::FirstWins; ///< First-definition or rejection policy for duplicate class definitions.
  bool collect_shadows = false; ///< Whether provenance of shadowed class definitions is retained.
  std::uint16_t target_release = 0; ///< Java release ceiling and multi-release selection target; zero selects base-only lookup.
  std::unordered_map<std::string, std::vector<std::shared_ptr<const detail::MemoryClass>>, detail::NameHash, std::equal_to<>> memory; ///< Copied in-memory definitions grouped by internal class name.
  std::size_t memory_classes = 0; ///< Total registered in-memory definitions.
  std::size_t memory_bytes = 0; ///< Accounted bytes of registered in-memory metadata and declarations.
  std::vector<detail::ExternalSource> application; ///< Ordered application-tier external sources.
  std::vector<detail::ExternalSource> runtime; ///< Ordered runtime-tier external sources.
  std::size_t archive_count = 0; ///< Number of registered archive sources.
  std::size_t archive_entries = 0; ///< Total indexed directory entries across archive sources.
  std::size_t archive_index_bytes = 0; ///< Total accounted archive index bytes.
};

} // namespace cafe::classpath
