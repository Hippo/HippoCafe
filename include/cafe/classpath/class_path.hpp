/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup classpath
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>

#include <cafe/classpath/class_info.hpp>

namespace cafe {
class ClassFile;
}

namespace cafe::classpath {

/**
 * @brief Resource and cancellation limits for source registration, metadata lookup, and hierarchy
 * operations.
 * @ingroup classpath
 */
struct ClassPathLimits {
  std::uintmax_t max_archive_bytes = std::uintmax_t{1024} * 1024 * 1024; ///< Maximum archive size in bytes.
  std::size_t max_archives = 128; ///< Maximum registered archives across both source tiers.
  std::size_t max_sources = 256; ///< Maximum registered external class sources.
  std::size_t max_archive_entries = 150'000; ///< Maximum directory entries in one archive.
  std::size_t max_total_archive_entries = 500'000; ///< Maximum directory entries across registered archives.
  std::size_t max_archive_index_bytes = std::size_t{128} * 1024 * 1024; ///< Maximum accounted archive index memory in bytes.
  std::size_t max_entry_compressed_bytes = std::size_t{32} * 1024 * 1024; ///< Maximum compressed bytes for one archive entry.
  std::size_t max_entry_uncompressed_bytes = std::size_t{16} * 1024 * 1024; ///< Maximum uncompressed bytes for one archive entry.
  std::size_t max_resolution_read_bytes = std::size_t{16} * 1024 * 1024; ///< Maximum bytes read for one class resolution.
  std::size_t max_compression_ratio = 1024; ///< Maximum permitted uncompressed-to-compressed size ratio.
  std::size_t max_manifest_bytes = std::size_t{256} * 1024; ///< Maximum bytes accepted for a JAR manifest.
  std::size_t max_class_bytes = std::size_t{8} * 1024 * 1024; ///< Maximum input class size for metadata/member scanning.
  std::size_t max_class_metadata_bytes = std::size_t{2} * 1024 * 1024; ///< Maximum accounted hierarchy metadata bytes for one class.
  std::size_t max_class_member_bytes = std::size_t{2} * 1024 * 1024; ///< Maximum accounted member declaration bytes for one class.
  std::size_t max_name_bytes = 4096; ///< Maximum encoded class/resource/entry name length in bytes.
  std::size_t max_path_depth = 128; ///< Maximum path components in a resource or entry name.
  std::size_t max_in_memory_classes = 65'535; ///< Maximum registered in-memory class definitions.
  std::size_t max_in_memory_metadata_bytes = std::size_t{32} * 1024 * 1024; ///< Maximum accounted bytes for registered in-memory metadata.
  std::size_t max_metadata_cache_bytes = std::size_t{32} * 1024 * 1024; ///< Maximum accounted bytes retained by the metadata cache.
  std::size_t max_hierarchy_depth = 256; ///< Maximum depth of a superclass/interface walk.
  std::size_t max_hierarchy_nodes = 4096; ///< Maximum nodes in one hierarchy closure.
  std::size_t max_hierarchy_edges = 65'536; ///< Maximum edges examined in a hierarchy closure.
  std::size_t max_hierarchy_work_bytes = std::size_t{32} * 1024 * 1024; ///< Maximum accounted temporary hierarchy-work bytes.
  std::size_t max_hierarchy_cache_bytes = std::size_t{32} * 1024 * 1024; ///< Maximum accounted retained hierarchy-cache bytes.
  std::size_t max_pair_cache_entries = 32'768; ///< Maximum memoized reference-pair queries.
  std::stop_token cancellation; ///< Cooperative stop token checked during bounded work.
};

/**
 * @brief Selection policy when multiple sources define the same class.
 * @ingroup classpath
 */
enum class DuplicatePolicy {
  FirstWins, ///< Select the first definition in deterministic source order.
  Reject, ///< Report conflicting definitions as an error.
};
/**
 * @brief External class-source precedence tier.
 * @ingroup classpath
 */
enum class SourceTier {
  Application, ///< Application sources, searched before runtime sources.
  Runtime, ///< Explicit runtime dependencies, searched after application sources.
};

/**
 * @brief Immutable ordered source snapshot. Copies share storage and pinned archive handles.
 * In-memory metadata is copied; external files and directory contents must remain stable for the
 * snapshot lifetime.
 * @ingroup classpath
 *
 * The snapshot pins archive handles and copies in-memory class metadata.
 * Keep external source files and directory contents stable for its lifetime.
 * Rebuilding the snapshot creates a separate cache of resolved definitions.
 */
class ClassPath {
private:
  struct Data;
  /**
   * @brief Share immutable registered source storage.
   * @param sources Shared immutable classpath registration storage.
   */
  explicit ClassPath(std::shared_ptr<const Data> sources);

  friend class ClassPathBuilder;
  friend class ClassResolver;
  std::shared_ptr<const Data> sources; ///< Shared immutable source registrations and pinned archive indexes.
};

/**
 * @brief Mutable registration builder for classpath snapshots. build() copies the registration
 * state; later builder edits do not change existing snapshots. Registration may allocate and throw
 * std::bad_alloc; concurrent mutation requires external synchronization.
 * @ingroup classpath
 */
class ClassPathBuilder {
public:
  /**
   * @brief Create an empty classpath builder with the supplied resource policy.
   * @param limits Resource ceilings and cooperative cancellation policy.
   */
  explicit ClassPathBuilder(ClassPathLimits limits = {});
  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~ClassPathBuilder();
  /**
   * @brief Transfer ownership without relocating the backing storage; existing references follow the
   * destination owner.
   */
  ClassPathBuilder(ClassPathBuilder&&) noexcept;
  /**
   * @brief Replace owned state by move assignment; views of the previous destination state are
   * invalidated.
   * @return This object after ownership transfer.
   */
  ClassPathBuilder& operator=(ClassPathBuilder&&) noexcept;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ClassPathBuilder(const ClassPathBuilder&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ClassPathBuilder& operator=(const ClassPathBuilder&) = delete;

  /**
   * @brief Select multi-release JAR entries and a class-version ceiling.
   *
   * Zero selects base entries and imposes no release ceiling. A nonzero target selects the highest
   * applicable manifest-enabled multi-release entry.
   * @param release Java release number; zero selects base-only lookup without a version ceiling.
   *
   * Zero selects base JAR entries without imposing a target version ceiling.
   */
  void set_target_release(std::uint16_t release);
  /**
   * @brief Choose first-definition-wins or duplicate rejection and optional shadow reporting.
   * @param policy First-definition selection or duplicate-rejection policy.
   * @param collect_shadows Whether to retain provenance of definitions hidden by the selected one.
   */
  void set_duplicate_policy(DuplicatePolicy policy, bool collect_shadows = false);

  /**
   * @brief Copy current class hierarchy and member metadata into the memory tier.
   * @param file Class whose current declaration metadata is copied.
   * @return Success or an invalid-metadata/resource error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_class(const ClassFile& file);
  /**
   * @brief Register an application class directory without loading all its classes.
   *
   * The registered root may be a symlink. Lookup paths below it must contain only regular files and
   * directories, with no symlink components.
   * @param root Registered class-directory root.
   * @return Success or a path/source-limit error.
   *
   * Explicitly registered roots can be symlinks. Paths under a root may
   * contain regular files and directories only, with no symlink components.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_directory(const std::filesystem::path& root);
  /**
   * @brief Index an application or runtime JAR, including its multi-release manifest.
   * @param path Source or destination filesystem path.
   * @param tier Application or runtime source tier; application sources precede runtime sources.
   * @return Success or an archive/source-limit error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_jar(
      const std::filesystem::path& path, SourceTier tier = SourceTier::Application);
  /**
   * @brief Index the classes section of an application or runtime JMOD.
   * @param path Source or destination filesystem path.
   * @param tier Application or runtime source tier; application sources precede runtime sources.
   * @return Success or an archive/source-limit error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_jmod(
      const std::filesystem::path& path, SourceTier tier = SourceTier::Application);
  /**
   * @brief Register runtime archives from an explicitly selected Java installation.
   *
   * Modular JDKs register java.base.jmod first, then remaining JMODs in filename order. Legacy
   * layouts use jre/lib or lib with rt.jar first. Linked runtime images without supported archives
   * return UnsupportedRuntime.
   * @param path Source or destination filesystem path.
   * @return Success or an unsupported-runtime/I/O/archive/resource error.
   *
   * Modular JDKs use java.base.jmod first, followed by other JMODs in
   * filename order. Legacy JDKs use jre/lib or lib, with rt.jar first.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_java_home(const std::filesystem::path& path);

  /**
   * @brief Copy registrations into an immutable ordered snapshot.
   * @return A snapshot independent of later builder edits; archive handles remain shared.
   */
  [[nodiscard]] ClassPath build() const;

private:
  /**
   * @brief Index and register an archive in the requested source tier.
   * @param path Source or destination filesystem path.
   * @param kind Payload or error discriminator.
   * @param tier Application or runtime source tier; application sources precede runtime sources.
   * @return Success or an archive/source-limit error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> add_archive(
      const std::filesystem::path& path, SourceKind kind, SourceTier tier);

  std::unique_ptr<ClassPath::Data> sources; ///< Mutable source registrations; build() copies them into an immutable snapshot.
};

} // namespace cafe::classpath
