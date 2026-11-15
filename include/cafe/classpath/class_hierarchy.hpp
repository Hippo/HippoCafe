/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup classpath
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cafe/classpath/class_resolver.hpp>

namespace cafe::classpath {

/**
 * @brief Representable reference join plus alternative interface candidates. Ambiguous candidates
 * require downstream verification before frame emission.
 * @ingroup classpath
 */
struct JoinResult {
  /**
   * @brief Nearest common representable superclass or array type.
   *
   * For ambiguity, the nearest common superclass (or its array
   * type). A frame engine must validate downstream uses of the chosen type.
   */
  std::string type;
  std::vector<std::string> ambiguous_candidates; ///< Common interface alternatives requiring downstream verification.
};

/**
 * @brief Reference assignability and joins within an immutable resolver snapshot. Copies share
 * synchronized hierarchy caches. Ordinary references use modified UTF-8 internal names; arrays use
 * descriptors. Missing or cyclic hierarchy information is an error, never an implicit Object
 * fallback.
 * @ingroup classpath
 *
 * Ordinary references use internal JVM names in modified UTF-8. Array types
 * use descriptors. Both are interpreted within this resolution snapshot.
 */
class ClassHierarchy {
public:
  /**
   * @brief Create hierarchy caches backed by the supplied resolver.
   * @param resolver Resolver snapshot whose metadata and cache are shared.
   */
  explicit ClassHierarchy(ClassResolver resolver);
  /**
   * @brief Read the immutable hierarchy identity.
   * @return A stamp used to reject analysis results from another snapshot.
   */
  [[nodiscard]] std::uint64_t snapshot_id() const noexcept;

  /**
   * @brief Create a hierarchy overlay in which the supplied class takes precedence.
   * @param owner Class owning the method, constants, or source slices being inspected.
   * @return A new hierarchy with copied owner metadata, or a validation/resource error.
   *
   * The class takes precedence for this hierarchy. Its metadata is copied
   * and the base resolver's cache can be shared across transformations.
   */
  [[nodiscard]] std::expected<ClassHierarchy, ResolutionError> with_class(const ClassFile& owner) const;
  /**
   * @brief Resolve one internal JVM class name in this snapshot.
   * @param name Borrowed JVM name in modified UTF-8.
   * @return Shared immutable metadata, or a named resolution failure; absence never fabricates a
   *   definition.
   */
  [[nodiscard]] std::expected<std::shared_ptr<const ClassInfo>, ResolutionError> resolve(std::string_view name) const;
  /**
   * @brief Read an exact declaration from the selected class's own member table.
   * @param owner Internal name of the class whose own declaration table is searched.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param descriptor JVM field or method descriptor in modified UTF-8.
   * @param method True for a method declaration, false for a field.
   * @return Access flags, an empty optional when absent, or a resolution failure; inherited members
   *   are not searched.
   *
   * Reads only member declarations in the selected definition. An empty
   * result means the name and descriptor are absent from its member table.
   */
  [[nodiscard]] std::expected<std::optional<std::uint16_t>, ResolutionError> declared_member_flags(
      std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const;
  /**
   * @brief Check JVM reference assignability within this hierarchy.
   * @param source Source reference type as a JVM internal name or array descriptor.
   * @param destination Destination reference type for assignment checking.
   * @return True if source can be assigned to destination, false if not, or a resolution error.
   */
  [[nodiscard]] std::expected<bool, ResolutionError> is_assignable(
      std::string_view source, std::string_view destination) const;
  /**
   * @brief Find a representable common reference type and interface alternatives.
   * @param a First reference type, expressed as an internal name or array descriptor.
   * @param b Second reference type, expressed as an internal name or array descriptor.
   * @return A deterministic join result, or an invalid-name/resolution/hierarchy error.
   */
  [[nodiscard]] std::expected<JoinResult, ResolutionError> join_reference_types(
      std::string_view a, std::string_view b) const;

private:
  struct State;
  std::shared_ptr<State> state; ///< Shared resolver overlay and synchronized hierarchy caches.
};

} // namespace cafe::classpath
