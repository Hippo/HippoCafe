/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup classpath
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <cafe/class_version.hpp>

namespace cafe::classpath {

/**
 * @brief Physical or in-memory origin of a class definition.
 * @ingroup classpath
 */
enum class SourceKind {
  Memory, ///< Copied in-memory class metadata.
  Directory, ///< Class file resolved beneath a registered directory.
  Jar, ///< ZIP-based Java archive.
  Jmod, ///< JMOD header plus ZIP entries in permitted module sections.
};

/**
 * @brief Owned location of a selected class definition, including its multi-release JAR selection.
 * @ingroup classpath
 */
struct SourceProvenance {
  SourceKind kind = SourceKind::Memory; ///< Discriminator selecting the interpretation of this record.
  std::filesystem::path path; ///< Source directory, archive, or registered in-memory source path.
  std::string entry; ///< Exact slash-separated archive entry name, when applicable.
  std::optional<std::uint16_t> release; ///< Selected multi-release JAR version; empty for an unversioned entry.
};

/**
 * @brief Class-source lookup, metadata, and hierarchy failures.
 * @ingroup classpath
 */
enum class ResolutionErrorKind {
  InvalidName, ///< Name is malformed, unsafe, or unsupported for the operation.
  NotFound, ///< No registered source defines the requested class.
  IoError, ///< Filesystem or stream operation failed.
  UnsafePath, ///< Lookup would traverse an unsafe path or unsupported file type.
  ArchiveCorrupt, ///< Archive framing or payload integrity checks failed.
  UnsupportedArchiveFeature, ///< Archive uses unsupported encryption, spanning, or other ZIP features.
  UnsupportedCompression, ///< Entry compression method is unsupported.
  EntryTooLarge, ///< Entry exceeds a configured size or compression-ratio ceiling.
  MalformedClassMetadata, ///< Required class/member metadata is malformed.
  ClassNameMismatch, ///< Declared class name differs from its lookup resource name.
  SourceConflict, ///< Duplicate or inconsistent definitions violate selection policy.
  UnsupportedClassVersion, ///< Declared version is outside the selected target or analysis profile.
  UnsupportedRuntime, ///< Java home provides no supported runtime archive layout.
  InvalidParent, ///< Superclass or interface relationship is structurally invalid.
  HierarchyCycle, ///< Inheritance traversal encountered a cycle.
  ResourceLimitExceeded, ///< Configured work, memory, count, or depth budget was exhausted.
  Cancelled, ///< The supplied stop token requested cancellation.
};

/**
 * @brief Class source or hierarchy failure with owned diagnostic and provenance data.
 * @ingroup classpath
 */
struct ResolutionError {
  ResolutionErrorKind kind; ///< Discriminator selecting the interpretation of this record.
  std::string name; ///< JVM internal or declaration name in modified UTF-8.
  std::filesystem::path path; ///< Filesystem path associated with the failing lookup, when available.
  std::string detail; ///< Owned human-readable explanation.
  std::optional<SourceProvenance> source = std::nullopt; ///< Provenance of the selected definition.
  std::vector<std::filesystem::path> search_path = {}; ///< Registered locations considered during resolution.
};

/**
 * @brief Owned hierarchy metadata for one selected class definition.
 *
 * Names use JVM internal spelling in modified UTF-8.
 * @ingroup classpath
 */
struct ClassInfo {
  std::string name; ///< JVM internal or declaration name in modified UTF-8.
  std::optional<std::string> super_name; ///< Direct superclass internal name; empty for Object or module-info.
  std::vector<std::string> interfaces; ///< Direct interface internal names in declaration order.
  std::uint16_t access_flags = 0; ///< JVM access bit mask.
  ClassVersion version; ///< Exact class-file major and minor version.
  SourceProvenance source; ///< Provenance of the selected definition.
  std::vector<SourceProvenance> shadowed_definitions; ///< Optional provenance of definitions hidden by the selected source.
};

} // namespace cafe::classpath
