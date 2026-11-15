#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <cafe/class_version.hpp>

namespace cafe::classpath {

enum class SourceKind { Memory,
                        Directory,
                        Jar,
                        Jmod };

struct SourceProvenance {
  SourceKind kind = SourceKind::Memory;
  std::filesystem::path path;
  std::string entry;
  std::optional<std::uint16_t> release;
};

enum class ResolutionErrorKind {
  InvalidName,
  NotFound,
  IoError,
  UnsafePath,
  ArchiveCorrupt,
  UnsupportedArchiveFeature,
  UnsupportedCompression,
  EntryTooLarge,
  MalformedClassMetadata,
  ClassNameMismatch,
  SourceConflict,
  UnsupportedClassVersion,
  UnsupportedRuntime,
  InvalidParent,
  HierarchyCycle,
  ResourceLimitExceeded,
  Cancelled,
};

struct ResolutionError {
  ResolutionErrorKind kind;
  std::string name;
  std::filesystem::path path;
  std::string detail;
  std::optional<SourceProvenance> source = std::nullopt;
  std::vector<std::filesystem::path> search_path = {};
};

struct ClassInfo {
  std::string name;
  std::optional<std::string> super_name;
  std::vector<std::string> interfaces;
  std::uint16_t access_flags = 0;
  ClassVersion version;
  SourceProvenance source;
  std::vector<SourceProvenance> shadowed_definitions;
};

} // namespace cafe::classpath
