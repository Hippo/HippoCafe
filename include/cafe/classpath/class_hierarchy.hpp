#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cafe/classpath/class_resolver.hpp>

namespace cafe::classpath {

struct JoinResult {
  // For ambiguity, the nearest common superclass (or its array
  // type). A frame engine must validate downstream uses of the chosen type.
  std::string type;
  std::vector<std::string> ambiguous_candidates;
};

// Ordinary references use internal JVM names in modified UTF-8. Array types
// use descriptors. Both are interpreted within this resolution snapshot.
class ClassHierarchy {
public:
  explicit ClassHierarchy(ClassResolver resolver);
  [[nodiscard]] std::uint64_t snapshot_id() const noexcept;

  // The class takes precedence for this hierarchy. Its metadata is copied
  // and the base resolver's cache can be shared across transformations.
  [[nodiscard]] std::expected<ClassHierarchy, ResolutionError> with_class(const ClassFile& owner) const;
  [[nodiscard]] std::expected<std::shared_ptr<const ClassInfo>, ResolutionError> resolve(std::string_view name) const;
  // Reads only member declarations in the selected definition. An empty
  // result means the name and descriptor are absent from its member table.
  [[nodiscard]] std::expected<std::optional<std::uint16_t>, ResolutionError> declared_member_flags(
      std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const;
  [[nodiscard]] std::expected<bool, ResolutionError> is_assignable(
      std::string_view source, std::string_view destination) const;
  [[nodiscard]] std::expected<JoinResult, ResolutionError> join_reference_types(
      std::string_view a, std::string_view b) const;

private:
  struct State;
  std::shared_ptr<State> state;
};

} // namespace cafe::classpath
