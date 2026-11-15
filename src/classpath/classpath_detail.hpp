/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup implementation
 */

#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string_view>

#include <cafe/classpath/class_path.hpp>

namespace cafe::classpath::detail {

inline constexpr std::string_view OBJECT = "java/lang/Object"; ///< JVM java/lang/Object identity used for reference-type rules.
inline constexpr std::string_view CLONEABLE = "java/lang/Cloneable"; ///< JVM java/lang/Cloneable identity, a universal array superinterface.
inline constexpr std::string_view SERIALIZABLE = "java/io/Serializable"; ///< JVM java/io/Serializable identity, a universal array superinterface.

/**
 * @brief Transparent hash allowing borrowed string views to query owned name sets without
 * allocating.
 * @ingroup implementation
 */
struct NameHash {
  /**
   * @brief Enables heterogeneous lookup using borrowed names or pair views.
   */
  using is_transparent = void;

  /**
   * @brief Hash borrowed text using the same hash as owned-name lookups.
   * @param value Borrowed name whose characters are hashed.
   * @return The deterministic standard string-view hash for this process.
   */
  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

/**
 * @brief Construct the operation's structured failure while owning diagnostic text.
 * @param kind Payload or error discriminator.
 * @param path Source or destination filesystem path.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected result carrying the appropriate error classification and location.
 * @ingroup implementation
 */
[[nodiscard]] inline std::unexpected<ResolutionError> fail(
    ResolutionErrorKind kind, const std::filesystem::path& path, std::string_view detail) {
  return std::unexpected(ResolutionError{kind, {}, path, std::string(detail)});
}

/**
 * @brief Construct the operation's structured failure while owning diagnostic text.
 * @param kind Payload or error discriminator.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param source Provenance copied into returned metadata or errors.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected result carrying the appropriate error classification and location.
 * @ingroup implementation
 */
[[nodiscard]] inline std::unexpected<ResolutionError> fail(
    ResolutionErrorKind kind, std::string_view name, const SourceProvenance& source, std::string_view detail) {
  return std::unexpected(ResolutionError{kind, std::string(name), source.path, std::string(detail), source});
}

/**
 * @brief Convert a validated modified UTF-8 internal class name to a safe ordinary UTF-8 resource
 * path.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return Owned resource spelling, or an invalid-name/size error.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<std::string, ResolutionError> resource_name(
    std::string_view name, const ClassPathLimits& limits);

/**
 * @brief Scan hierarchy metadata without decoding methods or optional attributes.
 * @param bytes Class-file buffer borrowed only for this call; returned metadata owns its strings.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param source Provenance copied into returned metadata or errors.
 * @return Owned metadata, or a bounded structural/resource failure.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<ClassInfo, ResolutionError> read_class_metadata(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, const SourceProvenance& source);
/**
 * @brief Copy hierarchy metadata from an in-memory class with the same lookup limits.
 * @param file Class whose current declaration metadata is copied.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return Owned metadata, or a validation/resource failure.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<ClassInfo, ResolutionError> copy_class_metadata(
    const ClassFile& file, const ClassPathLimits& limits);
/**
 * @brief Enforce an explicit target Java release against the selected class version.
 * @param info Selected class or archive metadata used by the operation.
 * @param target_release Explicit Java release ceiling; zero disables the ceiling and versioned
 *   selection.
 * @return Success when compatible or no ceiling is configured; otherwise UnsupportedClassVersion.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<void, ResolutionError> check_class_release(
    const ClassInfo& info, std::uint16_t target_release);
/**
 * @brief Account for owned class metadata and provenance storage.
 * @param info Selected class or archive metadata used by the operation.
 * @return The accounted byte count used by source/cache limits.
 * @ingroup implementation
 */
[[nodiscard]] std::size_t metadata_size(const ClassInfo& info);

/**
 * @brief Owned declaration name, descriptor, and access bits for exact member queries.
 * @ingroup implementation
 */
struct MemberDecl {
  std::string name; ///< JVM internal or declaration name in modified UTF-8.
  std::string descriptor; ///< JVM field or method descriptor.
  std::uint16_t flags; ///< JVM access bit mask.
};

/**
 * @brief Owned sorted field and method declaration tables with accounted cache size.
 * @ingroup implementation
 */
struct DeclaredMembers {
  std::vector<MemberDecl> fields; ///< Sorted owned field declaration records.
  std::vector<MemberDecl> methods; ///< Sorted owned method declaration records.
  std::size_t byte_size; ///< Accounted bytes occupied by this metadata/declaration table.

  /**
   * @brief Binary-search the selected field or method declaration table.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param descriptor JVM field or method descriptor, or its UTF-8 constant-pool index.
   * @param method True selects the method table; false selects fields.
   * @return Access bits, or nullopt if no exact name/descriptor match exists.
   */
  [[nodiscard]] std::optional<std::uint16_t> flags(
      std::string_view name, std::string_view descriptor, bool method) const;
};

/**
 * @brief Scan bounded declaration tables and verify they still match selected class identity.
 * @param bytes Class-file buffer borrowed only for this call; returned metadata owns its strings.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param selected Previously selected class identity against which metadata stability is checked.
 * @return Owned sorted declarations, or a structural/change/resource failure.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<DeclaredMembers, ResolutionError> read_class_members(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, const ClassInfo& selected);
/**
 * @brief Copy declaration tables from an in-memory class under member-storage limits.
 * @param file Class whose current declaration metadata is copied.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param selected Previously selected class identity against which metadata stability is checked.
 * @return Owned sorted declarations, or an identity/resource failure.
 * @ingroup implementation
 */
[[nodiscard]] std::expected<DeclaredMembers, ResolutionError> copy_class_members(
    const ClassFile& file, const ClassPathLimits& limits, const ClassInfo& selected);

} // namespace cafe::classpath::detail
