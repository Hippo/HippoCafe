/**
 * @file
 * @brief Class source snapshots, metadata resolution, and reference-type relationships.
 * @ingroup implementation
 */

#include <cafe/classpath/class_resolver.hpp>

#include <algorithm>
#include <mutex>
#include <utility>

#include "class_path_storage.hpp"

namespace cafe::classpath {
namespace {

/**
 * @brief Shared immutable selected class hierarchy metadata.
 */
using Metadata = std::shared_ptr<const ClassInfo>;

/**
 * @brief Selected archive entry paired with its shared owning source and provenance.
 * @ingroup implementation
 */
struct ArchiveClass {
  SourceProvenance source; ///< Provenance of the selected definition.
  const detail::ArchiveSource* archive; ///< Borrowed archive source pinned by the resolver snapshot.
};

/**
 * @brief Selected in-memory, directory, or archive definition with stable ownership.
 */
using Candidate = std::variant<Metadata, ArchiveClass, detail::DirectoryClass>;

/**
 * @brief Read the selected candidate's source identity without loading its class body.
 * @param candidate Selected directory/archive/in-memory class candidate.
 * @return Owned provenance for diagnostics and shadow reporting.
 * @ingroup implementation
 */
const SourceProvenance& provenance(const Candidate& candidate) {
  if (const auto* metadata = std::get_if<Metadata>(&candidate))
    return (*metadata)->source;
  if (const auto* archive = std::get_if<ArchiveClass>(&candidate))
    return archive->source;
  return std::get<detail::DirectoryClass>(candidate).source;
}

} // namespace

/**
 * @brief Shared resolver snapshot, synchronized metadata/member caches, provenance candidates, and
 * cumulative read counters.
 * @ingroup implementation
 */
struct ClassResolver::State {
  /**
   * @brief Initialize shared resolver snapshot, synchronized metadata/member caches, provenance
   * candidates, and cumulative read counters.
   * @param path Immutable class-source snapshot retained by this resolver.
   */
  explicit State(ClassPath path) : class_path(std::move(path)) {
  }

  /**
   * @brief Read a selected archive candidate while updating shared resolution counters.
   * @param archive Retained source archive supplying original payloads.
   * @param source Provenance copied into returned metadata or errors.
   * @return Owned class bytes, or a bounded archive/read error.
   */
  [[nodiscard]] std::expected<std::vector<std::byte>, ResolutionError> read_archive(
      const detail::ArchiveSource& archive, const SourceProvenance& source);

  ClassPath class_path; ///< Immutable source snapshot pinned by this resolver state.
  std::mutex mutex; ///< Serializes shared mutable cache or stream state.
  std::unordered_map<std::string, Metadata, detail::NameHash, std::equal_to<>> cache; ///< Shared selected class metadata cache indexed by internal name.
  std::unordered_map<std::string, detail::DeclaredMembers, detail::NameHash, std::equal_to<>> members; ///< Owned or shared declaration tables for exact member lookup.
  std::size_t cache_bytes = 0; ///< Accounted bytes currently retained by this cache.
  ResolutionStats stats; ///< Cumulative synchronized resolver activity counters.
};

std::expected<std::vector<std::byte>, ResolutionError> ClassResolver::State::read_archive(
    const detail::ArchiveSource& archive, const SourceProvenance& source) {
  const auto& limits = class_path.sources->limits;
  ++stats.archive_entries_read;
  auto bytes = archive.zip.read(source.entry, std::min(limits.max_class_bytes, limits.max_resolution_read_bytes));
  const auto* entry = archive.zip.find(source.entry);
  if (bytes && entry && entry->is_deflated())
    ++stats.archive_entries_inflated;
  return bytes;
}

ClassResolver::ClassResolver(ClassPath class_path) : state(std::make_shared<State>(std::move(class_path))) {
}

const ClassPathLimits& ClassResolver::limits() const {
  return state->class_path.sources->limits;
}

std::uint16_t ClassResolver::target_release() const {
  return state->class_path.sources->target_release;
}

ResolutionStats ClassResolver::stats() const {
  std::scoped_lock guard(state->mutex);
  return state->stats;
}

std::expected<Metadata, ResolutionError> ClassResolver::resolve(std::string_view name) const {
  const auto& sources = *state->class_path.sources;
  if (sources.limits.cancellation.stop_requested())
    return detail::fail(ResolutionErrorKind::Cancelled, name.substr(0, sources.limits.max_name_bytes), {}, "class lookup cancelled");
  if (name.size() > sources.limits.max_name_bytes)
    return detail::fail(ResolutionErrorKind::ResourceLimitExceeded, name.substr(0, sources.limits.max_name_bytes), {},
                        "class name exceeds its configured length limit");
  std::scoped_lock guard(state->mutex);
  if (const auto hit = state->cache.find(name); hit != state->cache.end()) {
    ++state->stats.cache_hits;
    return hit->second;
  }

  auto resource = detail::resource_name(name, sources.limits);
  if (!resource)
    return std::unexpected(std::move(resource.error()));
  *resource += ".class";
  const auto report = [&](ResolutionError error) {
    error.name = name;
    for (auto* list : {&sources.application, &sources.runtime}) {
      for (const auto& candidate : *list) {
        if (const auto* directory = std::get_if<detail::DirectorySource>(&candidate))
          error.search_path.push_back(directory->root);
        else
          error.search_path.push_back(std::get<1>(candidate)->path);
      }
    }
    return std::unexpected(std::move(error));
  };
  const bool scan_all = sources.duplicates == DuplicatePolicy::Reject || sources.collect_shadows;
  std::optional<Candidate> selected;
  std::vector<SourceProvenance> shadows;
  std::size_t shadow_bytes = 0;
  const auto consider = [&](Candidate candidate) -> std::expected<void, ResolutionError> {
    if (!selected) {
      selected.emplace(std::move(candidate));
    } else if (sources.duplicates == DuplicatePolicy::Reject) {
      return detail::fail(ResolutionErrorKind::SourceConflict, name, provenance(candidate),
                          "multiple definitions in strict classpath snapshot");
    } else if (sources.collect_shadows) {
      auto source = provenance(candidate);
      const auto bytes = sizeof(source) + source.entry.size() +
                         source.path.native().size() * sizeof(std::filesystem::path::value_type);
      if (bytes > sources.limits.max_class_metadata_bytes - shadow_bytes)
        return detail::fail(ResolutionErrorKind::ResourceLimitExceeded, name, source,
                            "duplicate diagnostics exceed their configured metadata limit");
      shadow_bytes += bytes;
      shadows.push_back(std::move(source));
    }
    return {};
  };

  if (const auto entry = sources.memory.find(name); entry != sources.memory.end()) {
    for (const auto& definition : entry->second) {
      if (auto added = consider(definition->info); !added)
        return report(std::move(added.error()));
      if (!scan_all)
        break;
    }
  }
  if (!selected || scan_all) {
    for (auto* list : {&sources.application, &sources.runtime}) {
      for (const auto& candidate : *list) {
        if (selected && !scan_all)
          break;
        if (sources.limits.cancellation.stop_requested())
          return detail::fail(ResolutionErrorKind::Cancelled, name, {}, "class lookup cancelled");

        if (const auto* directory = std::get_if<detail::DirectorySource>(&candidate)) {
          auto entry = detail::DirectoryClass::find(*directory, *resource);
          if (!entry)
            return report(std::move(entry.error()));
          if (*entry)
            if (auto added = consider(std::move(**entry)); !added)
              return report(std::move(added.error()));
        } else {
          const auto& archive = *std::get<1>(candidate);
          auto entry = archive.find(*resource, sources.target_release);
          if (entry)
            if (auto added = consider(ArchiveClass{std::move(*entry), &archive}); !added)
              return report(std::move(added.error()));
        }
      }
    }
  }
  if (!selected)
    return report({ResolutionErrorKind::NotFound, std::string(name), {}, "class is absent from the configured classpath"});

  Metadata resolved;
  if (auto* in_memory = std::get_if<Metadata>(&*selected)) {
    if (shadows.empty()) {
      resolved = std::move(*in_memory);
    } else {
      auto info = std::make_shared<ClassInfo>(**in_memory);
      info->shadowed_definitions = std::move(shadows);
      resolved = std::move(info);
    }
  } else {
    const auto& source = provenance(*selected);
    const auto read = [&]() -> std::expected<std::vector<std::byte>, ResolutionError> {
      if (auto* directory = std::get_if<detail::DirectoryClass>(&*selected))
        return directory->read(sources.limits);
      const auto& archive = std::get<ArchiveClass>(*selected);
      return state->read_archive(*archive.archive, source);
    };
    auto bytes = read();
    if (!bytes)
      return report(std::move(bytes.error()));
    ++state->stats.metadata_scans;
    auto info = detail::read_class_metadata(*bytes, sources.limits, source);
    if (!info)
      return report(std::move(info.error()));
    if (info->name != name)
      return report({ResolutionErrorKind::ClassNameMismatch, std::string(name), source.path,
                     "entry declares " + info->name + " instead of the requested class", source});
    info->shadowed_definitions = std::move(shadows);
    resolved = std::make_shared<const ClassInfo>(std::move(*info));
  }

  if (auto compatible = detail::check_class_release(*resolved, sources.target_release); !compatible)
    return report(std::move(compatible.error()));
  const auto metadata_bytes = detail::metadata_size(*resolved);
  if (metadata_bytes > sources.limits.max_class_metadata_bytes)
    return report(detail::fail(ResolutionErrorKind::ResourceLimitExceeded, name, resolved->source,
                               "class metadata and duplicate diagnostics exceed their configured byte limit")
                      .error());
  const auto bytes = metadata_bytes + resolved->name.size() + 64;
  if (bytes <= sources.limits.max_metadata_cache_bytes - state->cache_bytes) {
    state->cache.emplace(resolved->name, resolved);
    state->cache_bytes += bytes;
  }
  return resolved;
}

std::expected<std::optional<std::uint16_t>, ResolutionError> ClassResolver::declared_member_flags(
    std::string_view owner, std::string_view name, std::string_view descriptor, bool method) const {
  auto info = resolve(owner);
  if (!info)
    return std::unexpected(std::move(info.error()));
  const auto& sources = *state->class_path.sources;
  const auto& source = (*info)->source;
  if (source.kind == SourceKind::Memory) {
    const auto definition = sources.memory.find(owner);
    return definition->second.front()->members.flags(name, descriptor, method);
  }

  std::scoped_lock guard(state->mutex);
  if (const auto known = state->members.find(owner); known != state->members.end())
    return known->second.flags(name, descriptor, method);
  const auto read = [&]() -> std::expected<std::vector<std::byte>, ResolutionError> {
    for (const auto* list : {&sources.application, &sources.runtime}) {
      for (const auto& candidate : *list) {
        if (const auto* directory = std::get_if<detail::DirectorySource>(&candidate)) {
          if (source.kind != SourceKind::Directory || source.path != directory->root)
            continue;
          auto file = detail::DirectoryClass::find(*directory, source.entry);
          if (!file)
            return std::unexpected(std::move(file.error()));
          if (*file)
            return (**file).read(sources.limits);
        } else {
          const auto& archive = *std::get<1>(candidate);
          if (source.kind == archive.kind && source.path == archive.path && archive.zip.find(source.entry))
            return state->read_archive(archive, source);
        }
      }
    }
    return detail::fail(ResolutionErrorKind::NotFound, owner, source, "member owner's class bytes are no longer available");
  };
  auto bytes = read();
  if (!bytes)
    return std::unexpected(std::move(bytes.error()));
  ++state->stats.member_scans;
  auto members = detail::read_class_members(*bytes, sources.limits, **info);
  if (!members)
    return std::unexpected(std::move(members.error()));
  const auto found = members->flags(name, descriptor, method);
  const auto size = members->byte_size + owner.size() + 64;
  if (size <= sources.limits.max_metadata_cache_bytes - state->cache_bytes) {
    state->members.emplace(owner, std::move(*members));
    state->cache_bytes += size;
  }
  return found;
}

} // namespace cafe::classpath
