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

struct ClassPathLimits {
  std::uintmax_t max_archive_bytes = 1024 * 1024 * 1024;
  std::size_t max_archives = 128;
  std::size_t max_sources = 256;
  std::size_t max_archive_entries = 150'000;
  std::size_t max_total_archive_entries = 500'000;
  std::size_t max_archive_index_bytes = 128 * 1024 * 1024;
  std::size_t max_entry_compressed_bytes = 32 * 1024 * 1024;
  std::size_t max_entry_uncompressed_bytes = 16 * 1024 * 1024;
  std::size_t max_resolution_read_bytes = 16 * 1024 * 1024;
  std::size_t max_compression_ratio = 1024;
  std::size_t max_manifest_bytes = 256 * 1024;
  std::size_t max_class_bytes = 8 * 1024 * 1024;
  std::size_t max_class_metadata_bytes = 2 * 1024 * 1024;
  std::size_t max_class_member_bytes = 2 * 1024 * 1024;
  std::size_t max_name_bytes = 4096;
  std::size_t max_path_depth = 128;
  std::size_t max_in_memory_classes = 65'535;
  std::size_t max_in_memory_metadata_bytes = 32 * 1024 * 1024;
  std::size_t max_metadata_cache_bytes = 32 * 1024 * 1024;
  std::size_t max_hierarchy_depth = 256;
  std::size_t max_hierarchy_nodes = 4096;
  std::size_t max_hierarchy_edges = 65'536;
  std::size_t max_hierarchy_work_bytes = 32 * 1024 * 1024;
  std::size_t max_hierarchy_cache_bytes = 32 * 1024 * 1024;
  std::size_t max_pair_cache_entries = 32'768;
  std::stop_token cancellation;
};

enum class DuplicatePolicy { FirstWins,
                             Reject };
enum class SourceTier { Application,
                        Runtime };

// The snapshot pins archive handles and copies in-memory class metadata.
// Keep external source files and directory contents stable for its lifetime.
// Rebuilding the snapshot creates a separate cache of resolved definitions.
class ClassPath {
private:
  struct Data;
  explicit ClassPath(std::shared_ptr<const Data> sources);

  friend class ClassPathBuilder;
  friend class ClassResolver;
  std::shared_ptr<const Data> sources;
};

class ClassPathBuilder {
public:
  explicit ClassPathBuilder(ClassPathLimits limits = {});
  ~ClassPathBuilder();
  ClassPathBuilder(ClassPathBuilder&&) noexcept;
  ClassPathBuilder& operator=(ClassPathBuilder&&) noexcept;
  ClassPathBuilder(const ClassPathBuilder&) = delete;
  ClassPathBuilder& operator=(const ClassPathBuilder&) = delete;

  // Zero selects base JAR entries without imposing a target version ceiling.
  void set_target_release(std::uint16_t release);
  void set_duplicate_policy(DuplicatePolicy policy, bool collect_shadows = false);

  [[nodiscard]] std::expected<void, ResolutionError> add_class(const ClassFile& file);
  // Explicitly registered roots can be symlinks. Paths under a root may
  // contain regular files and directories only, with no symlink components.
  [[nodiscard]] std::expected<void, ResolutionError> add_directory(const std::filesystem::path& root);
  [[nodiscard]] std::expected<void, ResolutionError> add_jar(
      const std::filesystem::path& path, SourceTier tier = SourceTier::Application);
  [[nodiscard]] std::expected<void, ResolutionError> add_jmod(
      const std::filesystem::path& path, SourceTier tier = SourceTier::Application);
  // Modular JDKs use java.base.jmod first, followed by other JMODs in
  // filename order. Legacy JDKs use jre/lib or lib, with rt.jar first.
  [[nodiscard]] std::expected<void, ResolutionError> add_java_home(const std::filesystem::path& path);

  [[nodiscard]] ClassPath build() const;

private:
  [[nodiscard]] std::expected<void, ResolutionError> add_archive(
      const std::filesystem::path& path, SourceKind kind, SourceTier tier);

  std::unique_ptr<ClassPath::Data> sources;
};

} // namespace cafe::classpath
