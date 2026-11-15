#include "class_path_storage.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace cafe::classpath::detail {
namespace {

constexpr std::string_view MANIFEST = "META-INF/MANIFEST.MF";
constexpr std::string_view VERSION_PREFIX = "META-INF/versions/";

bool equals_ascii_folded(std::string_view left, std::string_view right) {
  const auto fold = [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
  };
  return std::ranges::equal(left, right, {}, fold, fold);
}

#ifdef _WIN32
struct NativeFile {
  explicit NativeFile(HANDLE handle) : handle(handle) {
  }
  NativeFile(NativeFile&& other) noexcept : handle(std::exchange(other.handle, INVALID_HANDLE_VALUE)) {
  }
  NativeFile& operator=(NativeFile&& other) noexcept {
    if (this != &other) {
      if (valid())
        CloseHandle(handle);
      handle = std::exchange(other.handle, INVALID_HANDLE_VALUE);
    }
    return *this;
  }
  ~NativeFile() {
    if (valid())
      CloseHandle(handle);
  }
  [[nodiscard]] bool valid() const {
    return handle != INVALID_HANDLE_VALUE;
  }
  HANDLE handle;
};

std::optional<std::wstring> normalized_path(HANDLE file) {
  const auto size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (size == 0)
    return std::nullopt;
  std::wstring result(size, L'\0');
  const auto used = GetFinalPathNameByHandleW(file, result.data(), size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (used == 0 || used >= size)
    return std::nullopt;
  result.resize(used);
  return result;
}
#else
struct NativeFile {
  explicit NativeFile(int handle) : handle(handle) {
  }
  NativeFile(NativeFile&& other) noexcept : handle(std::exchange(other.handle, -1)) {
  }
  NativeFile& operator=(NativeFile&& other) noexcept {
    if (this != &other) {
      if (valid())
        ::close(handle);
      handle = std::exchange(other.handle, -1);
    }
    return *this;
  }
  ~NativeFile() {
    if (valid())
      ::close(handle);
  }
  [[nodiscard]] bool valid() const {
    return handle >= 0;
  }
  int handle;
};
#endif

} // namespace

struct DirectorySource::Impl {
  NativeFile file;
#ifdef _WIN32
  std::wstring resolved;
#endif
};

struct DirectoryClass::File {
  NativeFile file;
  std::uintmax_t size;
};

std::expected<std::optional<DirectoryClass>, ResolutionError> DirectoryClass::find(
    const DirectorySource& directory, std::string_view resource) {
  const SourceProvenance source{SourceKind::Directory, directory.root, std::string(resource), {}};
  const auto io_error = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::IoError, {}, source, reason);
  };
  const auto symlink = [&] {
    return fail(ResolutionErrorKind::UnsafePath, {}, source, "classpath directory contains a symlink");
  };
#ifdef _WIN32
  auto path = std::filesystem::path(directory.handle->resolved);
  auto remaining = resource;
  while (!remaining.empty()) {
    const auto slash = remaining.find('/');
    const auto component = remaining.substr(0, slash);
    path /= std::filesystem::path(std::u8string(component.begin(), component.end()));
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return std::nullopt;
      return io_error("could not inspect classpath directory component");
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      return symlink();
    if (slash == std::string_view::npos)
      break;
    remaining.remove_prefix(slash + 1);
  }
  NativeFile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid() || GetFileType(file.handle) != FILE_TYPE_DISK)
    return io_error("classpath entry could not be opened as a regular file");
  const auto path_in_use = normalized_path(file.handle);
  LARGE_INTEGER size;
  FILE_ATTRIBUTE_TAG_INFO attributes;
  if (!path_in_use || !path_in_use->starts_with(directory.handle->resolved) ||
      !GetFileInformationByHandleEx(file.handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
      (attributes.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
      !GetFileSizeEx(file.handle, &size) || size.QuadPart < 0)
    return io_error("classpath entry escaped the directory root or is not a regular file");
  return DirectoryClass{source, std::make_shared<File>(File{std::move(file), static_cast<std::uintmax_t>(size.QuadPart)})};
#else
  NativeFile parent(-1);
  auto remaining = resource;
  while (true) {
    const auto slash = remaining.find('/');
    const bool is_file = slash == std::string_view::npos;
    const std::string component(remaining.substr(0, slash));
    const auto directory_fd = parent.valid() ? parent.handle : directory.handle->file.handle;
    const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (is_file ? 0 : O_DIRECTORY);
    NativeFile entry(::openat(directory_fd, component.c_str(), flags));
    if (!entry.valid()) {
      const auto error = errno;
      if (error == ENOENT)
        return std::nullopt;
      if (error == ELOOP)
        return symlink();
      if (error == ENOTDIR) {
        struct stat metadata{};
        if (::fstatat(directory_fd, component.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(metadata.st_mode))
          return symlink();
        return std::nullopt;
      }
      return io_error(std::generic_category().message(error));
    }
    if (is_file) {
      struct stat metadata{};
      if (::fstat(entry.handle, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size < 0)
        return io_error("classpath entry is not a readable regular file");
      return DirectoryClass{source, std::make_shared<File>(File{std::move(entry), static_cast<std::uintmax_t>(metadata.st_size)})};
    }
    parent = std::move(entry);
    remaining.remove_prefix(slash + 1);
  }
#endif
}

std::expected<std::vector<std::byte>, ResolutionError> DirectoryClass::read(const ClassPathLimits& limits) {
  if (file->size > std::min(limits.max_class_bytes, limits.max_resolution_read_bytes))
    return fail(ResolutionErrorKind::EntryTooLarge, {}, source, "class file exceeds the configured read budget");
  std::vector<std::byte> bytes(static_cast<std::size_t>(file->size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, {}, source, "class file read cancelled");
    const auto count = std::min<std::size_t>(64 * 1024, bytes.size() - offset);
#ifdef _WIN32
    DWORD used = 0;
    if (!ReadFile(file->file.handle, bytes.data() + offset, static_cast<DWORD>(count), &used, nullptr) || used == 0)
#else
    const auto used = ::read(file->file.handle, bytes.data() + offset, count);
    if (used < 0 && errno == EINTR)
      continue;
    if (used <= 0)
#endif
      return fail(ResolutionErrorKind::IoError, {}, source, "could not read the indexed class file length");
    offset += static_cast<std::size_t>(used);
  }
  return bytes;
}

std::expected<bool, ResolutionError> parse_multi_release_manifest(
    std::span<const std::byte> bytes, const SourceProvenance& source) {
  if (bytes.empty())
    return false;
  std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  bool continuing = false;
  bool multi_release = false;
  bool seen = false;
  std::string value;
  while (!text.empty()) {
    const auto delimiter = text.find_first_of("\r\n");
    const auto line = text.substr(0, delimiter);
    if (line.empty())
      break;
    if (delimiter == std::string_view::npos) {
      text = {};
    } else {
      const auto newline = text[delimiter] == '\r' && delimiter + 1 < text.size() && text[delimiter + 1] == '\n' ? 2u : 1u;
      text.remove_prefix(delimiter + newline);
    }
    if (line.front() == ' ') {
      if (!continuing)
        return fail(ResolutionErrorKind::ArchiveCorrupt, {}, source, "manifest continuation has no header");
      if (multi_release)
        value += line.substr(1);
    } else {
      const auto key_end = line.find(':');
      if (key_end == 0 || key_end == std::string_view::npos || key_end + 1 == line.size() || line[key_end + 1] != ' ')
        return fail(ResolutionErrorKind::ArchiveCorrupt, {}, source, "invalid manifest header");
      continuing = true;
      multi_release = equals_ascii_folded(line.substr(0, key_end), "Multi-Release");
      if (multi_release) {
        if (seen)
          return fail(ResolutionErrorKind::ArchiveCorrupt, {}, source, "duplicate manifest Multi-Release attribute");
        seen = true;
        value = line.substr(key_end + 2);
      }
    }
  }
  return seen && equals_ascii_folded(value, "true");
}

std::optional<SourceProvenance> ArchiveSource::find(std::string_view resource, std::uint16_t target_release) const {
  if (kind == SourceKind::Jar && resource.starts_with(VERSION_PREFIX))
    return std::nullopt;
  if (target_release >= 9 && !versions.empty()) {
    const auto entries = versions.find(resource);
    if (entries != versions.end()) {
      const auto& candidates = entries->second;
      const auto selected = std::ranges::upper_bound(candidates, target_release, {}, &VersionedClass::release);
      if (selected != candidates.begin()) {
        const auto& entry = *std::prev(selected);
        return SourceProvenance{kind, path, entry.entry->name, entry.release};
      }
    }
  }

  const auto jmod_name = kind == SourceKind::Jmod ? "classes/" + std::string(resource) : std::string{};
  const auto* root = zip.find(kind == SourceKind::Jmod ? std::string_view(jmod_name) : resource);
  if (root && !root->directory)
    return SourceProvenance{kind, path, root->name, {}};
  return std::nullopt;
}

} // namespace cafe::classpath::detail

namespace cafe::classpath {
namespace {

using detail::fail;

bool valid_path(const std::filesystem::path& path) {
  return !path.empty() && path.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}

struct ArchiveBudget {
  std::size_t count;
  std::size_t entries;
  std::size_t bytes;
};

std::expected<std::shared_ptr<const detail::ArchiveSource>, ResolutionError> load_archive(
    const std::filesystem::path& path, SourceKind kind, const ClassPathLimits& limits, ArchiveBudget& used) {
  if (!valid_path(path))
    return fail(ResolutionErrorKind::IoError, path, "archive path is empty or contains NUL");
  if (used.count >= limits.max_archives)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "archive count exceeds its configured limit");
  auto bounded = limits;
  bounded.max_archive_entries = std::min(limits.max_archive_entries, limits.max_total_archive_entries - used.entries);
  bounded.max_archive_index_bytes -= used.bytes;
  auto zip = archive::ZipArchive::open(path, kind, bounded);
  if (!zip)
    return std::unexpected(std::move(zip.error()));
  auto source = std::make_shared<detail::ArchiveSource>(
      detail::ArchiveSource{kind, path, std::move(*zip), {}, 0});
  source->index_bytes = source->zip.index_size();

  if (kind == SourceKind::Jar) {
    if (const auto manifest = source->zip.find(detail::MANIFEST); manifest && !manifest->directory) {
      auto bytes = source->zip.read(manifest->name, limits.max_manifest_bytes);
      if (!bytes)
        return std::unexpected(std::move(bytes.error()));
      const auto multi_release = detail::parse_multi_release_manifest(*bytes, {kind, path, manifest->name, {}});
      if (!multi_release)
        return std::unexpected(multi_release.error());
      if (*multi_release) {
        constexpr std::size_t INDEX_COST = sizeof(detail::VersionedClass) + 64;
        for (const auto& entry : source->zip.entries()) {
          if (limits.cancellation.stop_requested())
            return fail(ResolutionErrorKind::Cancelled, path, "multi-release JAR indexing cancelled");
          std::string_view name(entry.name);
          if (entry.directory || !name.starts_with(detail::VERSION_PREFIX) || !name.ends_with(".class"))
            continue;
          name.remove_prefix(detail::VERSION_PREFIX.size());
          const auto slash = name.find('/');
          if (slash == 0 || slash == std::string_view::npos || name.front() < '1' || name.front() > '9')
            continue;
          std::uint16_t release = 0;
          const auto end = name.data() + slash;
          const auto parsed = std::from_chars(name.data(), end, release);
          if (parsed.ec != std::errc{} || parsed.ptr != end || release < 9)
            continue;
          name.remove_prefix(slash + 1);
          if (name.starts_with("META-INF/"))
            continue;
          if (INDEX_COST > bounded.max_archive_index_bytes - source->index_bytes)
            return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "multi-release JAR index exceeds its byte limit");
          source->index_bytes += INDEX_COST;
          source->versions[name].push_back({release, &entry});
        }
        for (auto& [name, entries] : source->versions)
          std::ranges::sort(entries, {}, &detail::VersionedClass::release);
      }
    }
  }

  ++used.count;
  used.entries += source->zip.entries().size();
  used.bytes += source->index_bytes;
  return source;
}

bool file_absent(std::error_code error) {
  return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
}

std::expected<std::vector<std::filesystem::path>, ResolutionError> archives_in(
    const std::filesystem::path& directory, std::string_view extension, std::size_t max_archives, std::stop_token stop) {
  std::vector<std::filesystem::path> result;
  std::error_code error;
  std::filesystem::directory_iterator it(directory, error);
  if (file_absent(error))
    return result;
  const auto end = std::filesystem::directory_iterator{};
  for (; !error && it != end; it.increment(error)) {
    if (stop.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, directory, "runtime archive discovery cancelled");
    if (it->path().extension() != extension)
      continue;
    if (it->is_regular_file(error)) {
      if (result.size() == max_archives)
        return fail(ResolutionErrorKind::ResourceLimitExceeded, directory, "runtime archive count exceeds its configured limit");
      result.push_back(it->path());
    }
    if (file_absent(error))
      error.clear();
  }
  if (error)
    return fail(ResolutionErrorKind::IoError, directory, error.message());
  std::ranges::sort(result);
  return result;
}

} // namespace

ClassPath::ClassPath(std::shared_ptr<const Data> sources) : sources(std::move(sources)) {
}

ClassPathBuilder::ClassPathBuilder(ClassPathLimits limits) : sources(std::make_unique<ClassPath::Data>()) {
  sources->limits = limits;
}

ClassPathBuilder::~ClassPathBuilder() = default;
ClassPathBuilder::ClassPathBuilder(ClassPathBuilder&&) noexcept = default;
ClassPathBuilder& ClassPathBuilder::operator=(ClassPathBuilder&&) noexcept = default;

void ClassPathBuilder::set_target_release(std::uint16_t release) {
  sources->target_release = release;
}

void ClassPathBuilder::set_duplicate_policy(DuplicatePolicy policy, bool collect_shadows) {
  sources->duplicates = policy;
  sources->collect_shadows = collect_shadows;
}

std::expected<void, ResolutionError> ClassPathBuilder::add_class(const ClassFile& file) {
  if (sources->limits.cancellation.stop_requested())
    return fail(ResolutionErrorKind::Cancelled, {}, "class registration cancelled");
  if (sources->memory_classes >= sources->limits.max_in_memory_classes)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, {}, "in-memory class count exceeds its configured limit");
  auto metadata = detail::copy_class_metadata(file, sources->limits);
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));
  auto members = detail::copy_class_members(file, sources->limits, *metadata);
  if (!members)
    return std::unexpected(std::move(members.error()));
  const auto header_bytes = detail::metadata_size(*metadata) + metadata->name.size() + 64;
  const auto available = sources->limits.max_in_memory_metadata_bytes - sources->memory_bytes;
  if (header_bytes > available || members->byte_size > available - header_bytes)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, metadata->name, metadata->source,
                "in-memory class metadata exceeds its configured byte limit");
  auto& definitions = sources->memory[metadata->name];
  const auto size = header_bytes + members->byte_size;
  definitions.push_back(std::make_shared<const detail::MemoryClass>(
      detail::MemoryClass{std::make_shared<const ClassInfo>(std::move(*metadata)), std::move(*members)}));
  sources->memory_bytes += size;
  ++sources->memory_classes;
  return {};
}

std::expected<void, ResolutionError> ClassPathBuilder::add_directory(const std::filesystem::path& root) {
  if (sources->limits.cancellation.stop_requested())
    return fail(ResolutionErrorKind::Cancelled, root, "directory registration cancelled");
  if (sources->application.size() + sources->runtime.size() >= sources->limits.max_sources)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, root, "classpath source count exceeds its configured limit");
  if (!valid_path(root))
    return fail(ResolutionErrorKind::IoError, root, "directory path is empty or contains NUL");
#ifdef _WIN32
  detail::NativeFile file(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  BY_HANDLE_FILE_INFORMATION metadata;
  if (!file.valid() || !GetFileInformationByHandle(file.handle, &metadata) ||
      (metadata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    return fail(ResolutionErrorKind::IoError, root, "classpath root could not be opened as a directory");
  auto resolved = detail::normalized_path(file.handle);
  if (!resolved)
    return fail(ResolutionErrorKind::IoError, root, "classpath root lacks a normalized path");
  if (!resolved->ends_with(L'\\'))
    *resolved += L'\\';
  auto handle = std::make_shared<const detail::DirectorySource::Impl>(
      detail::DirectorySource::Impl{std::move(file), std::move(*resolved)});
#else
  detail::NativeFile file(::open(root.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  if (!file.valid())
    return fail(ResolutionErrorKind::IoError, root, std::generic_category().message(errno));
  auto handle = std::make_shared<const detail::DirectorySource::Impl>(detail::DirectorySource::Impl{std::move(file)});
#endif
  sources->application.emplace_back(detail::DirectorySource{root, std::move(handle)});
  return {};
}

std::expected<void, ResolutionError> ClassPathBuilder::add_jar(const std::filesystem::path& path, SourceTier tier) {
  return add_archive(path, SourceKind::Jar, tier);
}

std::expected<void, ResolutionError> ClassPathBuilder::add_jmod(const std::filesystem::path& path, SourceTier tier) {
  return add_archive(path, SourceKind::Jmod, tier);
}

std::expected<void, ResolutionError> ClassPathBuilder::add_archive(
    const std::filesystem::path& path, SourceKind kind, SourceTier tier) {
  if (sources->application.size() + sources->runtime.size() >= sources->limits.max_sources)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "classpath source count exceeds its configured limit");
  ArchiveBudget budget{sources->archive_count, sources->archive_entries, sources->archive_index_bytes};
  auto archive = load_archive(path, kind, sources->limits, budget);
  if (!archive)
    return std::unexpected(std::move(archive.error()));
  auto& list = tier == SourceTier::Application ? sources->application : sources->runtime;
  list.emplace_back(std::move(*archive));
  sources->archive_count = budget.count;
  sources->archive_entries = budget.entries;
  sources->archive_index_bytes = budget.bytes;
  return {};
}

std::expected<void, ResolutionError> ClassPathBuilder::add_java_home(const std::filesystem::path& path) {
  if (!valid_path(path))
    return fail(ResolutionErrorKind::IoError, path, "Java home path is empty or contains NUL");
  const auto& limits = sources->limits;
  const auto available = std::min(limits.max_archives - sources->archive_count,
                                  limits.max_sources - sources->application.size() - sources->runtime.size());
  auto archives = archives_in(path / "jmods", ".jmod", available, limits.cancellation);
  if (!archives)
    return std::unexpected(std::move(archives.error()));

  SourceKind kind = SourceKind::Jmod;
  if (!archives->empty()) {
    const auto base = std::ranges::find_if(*archives, [](const auto& archive) {
      return archive.filename() == "java.base.jmod";
    });
    if (base == archives->end())
      return fail(ResolutionErrorKind::UnsupportedRuntime, path, "JMOD directory lacks java.base.jmod");
    std::rotate(archives->begin(), base, base + 1);
  } else {
    kind = SourceKind::Jar;
    const auto legacy_root = [&](const auto& library) -> std::expected<bool, ResolutionError> {
      std::error_code error;
      const bool runtime = std::filesystem::is_regular_file(library / "rt.jar", error);
      if (error && !file_absent(error))
        return fail(ResolutionErrorKind::IoError, library, error.message());
      return runtime;
    };
    auto lib = path / "jre" / "lib";
    auto runtime = legacy_root(lib);
    if (!runtime)
      return std::unexpected(std::move(runtime.error()));
    if (!*runtime) {
      lib = path / "lib";
      runtime = legacy_root(lib);
      if (!runtime)
        return std::unexpected(std::move(runtime.error()));
    }
    if (!*runtime) {
      std::error_code error;
      const bool image = std::filesystem::is_regular_file(path / "lib" / "modules", error);
      return fail(ResolutionErrorKind::UnsupportedRuntime, path,
                  image ? "Java home contains only a linked runtime image (lib/modules)"
                        : "Java home contains neither JMODs nor a legacy rt.jar");
    }
    archives = archives_in(lib, ".jar", available, limits.cancellation);
    if (!archives)
      return std::unexpected(std::move(archives.error()));
    const auto rt = std::ranges::find(*archives, lib / "rt.jar");
    if (rt == archives->end())
      return fail(ResolutionErrorKind::IoError, lib, "rt.jar disappeared during discovery");
    std::rotate(archives->begin(), rt, rt + 1);
    auto extension = archives_in(lib / "ext", ".jar", available - archives->size(), limits.cancellation);
    if (!extension)
      return std::unexpected(std::move(extension.error()));
    archives->insert(archives->end(), extension->begin(), extension->end());
    if (lib != path / "lib") {
      auto development = archives_in(path / "lib", ".jar", available - archives->size(), limits.cancellation);
      if (!development)
        return std::unexpected(std::move(development.error()));
      archives->insert(archives->end(), development->begin(), development->end());
    }
  }

  ArchiveBudget budget{sources->archive_count, sources->archive_entries, sources->archive_index_bytes};
  std::vector<detail::ExternalSource> pending;
  pending.reserve(archives->size());
  for (const auto& archive_path : *archives) {
    auto archive = load_archive(archive_path, kind, limits, budget);
    if (!archive)
      return std::unexpected(std::move(archive.error()));
    pending.emplace_back(std::move(*archive));
  }
  sources->runtime.insert(sources->runtime.end(), std::make_move_iterator(pending.begin()),
                          std::make_move_iterator(pending.end()));
  sources->archive_count = budget.count;
  sources->archive_entries = budget.entries;
  sources->archive_index_bytes = budget.bytes;
  return {};
}

ClassPath ClassPathBuilder::build() const {
  return ClassPath(std::make_shared<const ClassPath::Data>(*sources));
}

} // namespace cafe::classpath
