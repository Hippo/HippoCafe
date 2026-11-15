#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>

#include <cafe/classpath/class_path.hpp>

namespace cafe::classpath {

struct ResolutionStats {
  std::size_t archive_entries_read = 0;
  std::size_t archive_entries_inflated = 0;
  std::size_t metadata_scans = 0;
  std::size_t member_scans = 0;
  std::size_t cache_hits = 0;
};

// A classpath-style resolution context. The cache and source order are
// specific to this snapshot. Copies share the snapshot and synchronized cache.
class ClassResolver {
public:
  explicit ClassResolver(ClassPath class_path);
  [[nodiscard]] std::expected<std::shared_ptr<const ClassInfo>, ResolutionError> resolve(std::string_view name) const;
  [[nodiscard]] ResolutionStats stats() const;

private:
  friend class ClassHierarchy;
  [[nodiscard]] const ClassPathLimits& limits() const;
  [[nodiscard]] std::uint16_t target_release() const;
  [[nodiscard]] std::expected<std::optional<std::uint16_t>, ResolutionError> declared_member_flags(
      std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const;

  struct State;
  std::shared_ptr<State> state;
};

} // namespace cafe::classpath
