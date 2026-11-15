/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup classpath
 */

#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>

#include <cafe/classpath/class_path.hpp>

namespace cafe::classpath {

/**
 * @brief Cumulative counters for the resolver's shared synchronized cache.
 * @ingroup classpath
 */
struct ResolutionStats {
  std::size_t archive_entries_read = 0; ///< Number of archive entry payload reads.
  std::size_t archive_entries_inflated = 0; ///< Number of Deflate entries decompressed.
  std::size_t metadata_scans = 0; ///< Number of class hierarchy metadata scans.
  std::size_t member_scans = 0; ///< Number of member declaration table scans.
  std::size_t cache_hits = 0; ///< Number of lookups served from the metadata cache.
};

/**
 * @brief Classpath-style resolver with lazy metadata reads and a synchronized cache shared by copies.
 *
 * This models an explicit namespace, not JVM class-loader delegation or module access. Returned
 * shared metadata outlives cache eviction.
 * @ingroup classpath
 *
 * A classpath-style resolution context. The cache and source order are
 * specific to this snapshot. Copies share the snapshot and synchronized cache.
 */
class ClassResolver {
public:
  /**
   * @brief Create a resolver and synchronized lazy cache for the supplied snapshot.
   * @param class_path Immutable ordered class-source snapshot.
   */
  explicit ClassResolver(ClassPath class_path);
  /**
   * @brief Resolve one internal JVM class name in this snapshot.
   * @param name Borrowed JVM name in modified UTF-8.
   * @return Shared immutable metadata, or a named resolution failure; absence never fabricates a
   *   definition.
   */
  [[nodiscard]] std::expected<std::shared_ptr<const ClassInfo>, ResolutionError> resolve(std::string_view name) const;
  /**
   * @brief Read synchronized cumulative resolution counters.
   * @return A value snapshot of shared-cache statistics.
   */
  [[nodiscard]] ResolutionStats stats() const;

private:
  friend class ClassHierarchy;
  /**
   * @brief Inspect the snapshot's resource policy.
   * @return A borrowed immutable limits reference.
   */
  [[nodiscard]] const ClassPathLimits& limits() const;
  /**
   * @brief Inspect the snapshot's Java target.
   * @return Zero for base-only selection without a version ceiling, or the chosen Java release.
   */
  [[nodiscard]] std::uint16_t target_release() const;
  /**
   * @brief Read an exact declaration from the selected class's own member table.
   * @param owner Internal name of the class whose own declaration table is searched.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param descriptor JVM field or method descriptor in modified UTF-8.
   * @param method True for a method declaration, false for a field.
   * @return Access flags, an empty optional when absent, or a resolution failure; inherited members
   *   are not searched.
   */
  [[nodiscard]] std::expected<std::optional<std::uint16_t>, ResolutionError> declared_member_flags(
      std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const;

  struct State;
  std::shared_ptr<State> state; ///< Shared snapshot and synchronized metadata cache.
};

} // namespace cafe::classpath
