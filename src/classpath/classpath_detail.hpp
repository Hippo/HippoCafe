#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string_view>

#include <cafe/classpath/class_path.hpp>

namespace cafe::classpath::detail {

inline constexpr std::string_view OBJECT = "java/lang/Object";
inline constexpr std::string_view CLONEABLE = "java/lang/Cloneable";
inline constexpr std::string_view SERIALIZABLE = "java/io/Serializable";

struct NameHash {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

[[nodiscard]] inline std::unexpected<ResolutionError> fail(
    ResolutionErrorKind kind, const std::filesystem::path& path, std::string_view detail) {
  return std::unexpected(ResolutionError{kind, {}, path, std::string(detail)});
}

[[nodiscard]] inline std::unexpected<ResolutionError> fail(
    ResolutionErrorKind kind, std::string_view name, const SourceProvenance& source, std::string_view detail) {
  return std::unexpected(ResolutionError{kind, std::string(name), source.path, std::string(detail), source});
}

[[nodiscard]] std::expected<std::string, ResolutionError> resource_name(
    std::string_view name, const ClassPathLimits& limits);

[[nodiscard]] std::expected<ClassInfo, ResolutionError> read_class_metadata(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, SourceProvenance source);
[[nodiscard]] std::expected<ClassInfo, ResolutionError> copy_class_metadata(
    const ClassFile& file, const ClassPathLimits& limits);
[[nodiscard]] std::expected<void, ResolutionError> check_class_release(
    const ClassInfo& info, std::uint16_t target_release);
[[nodiscard]] std::size_t metadata_size(const ClassInfo& info);

struct MemberDecl {
  std::string name;
  std::string descriptor;
  std::uint16_t flags;
};

struct DeclaredMembers {
  std::vector<MemberDecl> fields;
  std::vector<MemberDecl> methods;
  std::size_t byte_size;

  [[nodiscard]] std::optional<std::uint16_t> flags(
      std::string_view name, std::string_view descriptor, bool method) const;
};

[[nodiscard]] std::expected<DeclaredMembers, ResolutionError> read_class_members(
    std::span<const std::byte> bytes, const ClassPathLimits& limits, const ClassInfo& selected);
[[nodiscard]] std::expected<DeclaredMembers, ResolutionError> copy_class_members(
    const ClassFile& file, const ClassPathLimits& limits, const ClassInfo& selected);

} // namespace cafe::classpath::detail
