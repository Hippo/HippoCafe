/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#include <cafe/archive/archive.hpp>

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <utility>

#include "archive_file.hpp"
#include "classpath/classpath_detail.hpp"
#include "zip_format.hpp"
#include "zip_writer.hpp"

namespace cafe::archive {
namespace detail {

Error archive_error(classpath::ResolutionError source) {
  using enum classpath::ResolutionErrorKind;
  ErrorKind kind;
  switch (source.kind) {
    case IoError:
    case UnsafePath:
      kind = ErrorKind::IoError;
      break;
    case NotFound:
      kind = ErrorKind::EntryNotFound;
      break;
    case UnsupportedArchiveFeature:
      kind = ErrorKind::UnsupportedFeature;
      break;
    case UnsupportedCompression:
      kind = ErrorKind::UnsupportedCompression;
      break;
    case EntryTooLarge:
    case ResourceLimitExceeded:
      kind = ErrorKind::LimitExceeded;
      break;
    case Cancelled:
      kind = ErrorKind::Cancelled;
      break;
    default:
      kind = ErrorKind::InvalidArchive;
      break;
  }
  return {kind, std::move(source.path), source.source ? std::move(source.source->entry) : std::string{}, std::move(source.detail)};
}

} // namespace detail
namespace {

using namespace detail;
/**
 * @brief Owned contiguous byte buffer.
 */
using Bytes = std::vector<std::byte>;

constexpr std::size_t EDITOR_ENTRY_OVERHEAD = sizeof(Entry) + sizeof(StagedEntry) + 64; ///< Conservative accounted bytes for each editor entry and index record.

/**
 * @brief Archive staging operation selecting existence and directory rules.
 * @ingroup implementation
 */
enum class Edit {
  Add, ///< Stage a new file whose name must not already exist.
  Replace, ///< Replace an existing non-directory entry.
  Directory, ///< Class file resolved beneath a registered directory.
};

/**
 * @brief Translate editor limits into shared archive-reader limits.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @return A reader policy with the corresponding byte/count/path/cancellation ceilings.
 * @ingroup implementation
 */
classpath::ClassPathLimits reader_limits(const Limits& limits) {
  classpath::ClassPathLimits result;
  result.max_archive_bytes = limits.max_archive_bytes;
  result.max_archive_entries = limits.max_archive_entries;
  result.max_archive_index_bytes = limits.max_archive_index_bytes;
  result.max_entry_compressed_bytes = limits.max_entry_compressed_bytes;
  result.max_entry_uncompressed_bytes = limits.max_entry_uncompressed_bytes;
  result.max_compression_ratio = limits.max_compression_ratio;
  result.max_name_bytes = limits.max_name_bytes;
  result.max_path_depth = limits.max_path_depth;
  result.cancellation = limits.cancellation;
  return result;
}

/**
 * @brief Map explicit archive format to resolver source provenance.
 * @param format Explicit Jar or Jmod format.
 * @return Jar or Jmod source kind.
 * @ingroup implementation
 */
classpath::SourceKind source_kind(Format format) {
  return format == Format::Jmod ? classpath::SourceKind::Jmod : classpath::SourceKind::Jar;
}

/**
 * @brief Construct an archive-edit failure with owned source and entry context.
 * @param kind Payload or error discriminator.
 * @param path Source or destination filesystem path.
 * @param entry Archive entry name copied into the diagnostic.
 * @param reason Explanation copied into the structured failure.
 * @return An unexpected archive Error.
 * @ingroup implementation
 */
std::unexpected<Error> failure(ErrorKind kind, const std::filesystem::path& path,
                               std::string_view entry, std::string_view reason) {
  return std::unexpected(Error{kind, path, std::string(entry), std::string(reason)});
}

/**
 * @brief Compare signature filenames against an uppercase ASCII spelling.
 * @param name ASCII entry name to compare without case distinctions.
 * @param upper Uppercase ASCII comparison spelling.
 * @return True for an ASCII case-insensitive match.
 * @ingroup implementation
 */
bool folded_equal(std::string_view name, std::string_view upper) {
  return std::ranges::equal(name, upper, [](char left, char right) {
    return (left >= 'a' && left <= 'z' ? left - 'a' + 'A' : left) == right;
  });
}

/**
 * @brief Recognize top-level META-INF signature artifacts that invalidate edited JAR signatures.
 * @param entry Archive entry whose name is checked for a signature artifact.
 * @return True for signature files requiring explicit removal before saving edits.
 * @ingroup implementation
 */
bool signature_entry(const Entry& entry) {
  std::string_view name = entry.name;
  constexpr std::string_view PREFIX = "META-INF/";
  if (entry.directory || !folded_equal(name.substr(0, PREFIX.size()), PREFIX))
    return false;
  name.remove_prefix(PREFIX.size());
  if (name.contains('/'))
    return false;
  const auto dot = name.rfind('.');
  return folded_equal(name.substr(0, 4), "SIG-") ||
         (dot != std::string_view::npos &&
          (folded_equal(name.substr(dot), ".SF") || folded_equal(name.substr(dot), ".RSA") ||
           folded_equal(name.substr(dot), ".DSA") || folded_equal(name.substr(dot), ".EC")));
}

/**
 * @brief Check whether an entry belongs to a permitted JMOD section.
 * @param name Case-sensitive archive entry name with slash separators.
 * @return True for supported top-level module sections.
 * @ingroup implementation
 */
bool jmod_section(std::string_view name) {
  constexpr std::string_view SECTIONS[] = {"classes", "conf", "include", "legal", "man", "lib", "bin"};
  const auto slash = name.find('/');
  return slash != std::string_view::npos && std::ranges::contains(SECTIONS, name.substr(0, slash));
}

/**
 * @brief Validate ordinary UTF-8 archive path bytes, excluding invalid scalar encodings.
 * @param name Ordinary UTF-8 text to validate.
 * @return True for a complete valid UTF-8 sequence.
 * @ingroup implementation
 */
bool valid_utf8(std::string_view name) {
  while (!name.empty()) {
    const auto first = static_cast<unsigned char>(name.front());
    name.remove_prefix(1);
    if (first < 0x80)
      continue;
    if (first < 0xc2 || first > 0xf4)
      return false;
    const std::size_t tails = first < 0xe0 ? 1 : first < 0xf0 ? 2
                                                              : 3;
    if (name.size() < tails)
      return false;
    const auto second = static_cast<unsigned char>(name.front());
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
      return false;
    for (const unsigned char tail : name.substr(0, tails)) {
      if (tail < 0x80 || tail > 0xbf)
        return false;
    }
    name.remove_prefix(tails);
  }
  return true;
}

} // namespace

/**
 * @brief Owned archive editor tables, name index, staged payloads, retained source, and budget
 * accounting. Logical edit publication follows successful allocation and validation.
 * @ingroup implementation
 */
struct Archive::State {
  /**
   * @brief Initialize owned archive editor tables, name index, staged payloads, retained source, and
   * budget accounting
   * @param format Explicit Jar or Jmod format.
   * @param limits Resource ceilings and cooperative cancellation policy.
   * @param source Original archive pathname retained for diagnostics.
   */
  State(Format format, Limits limits, std::filesystem::path source = {}) :
      format(format), limits(std::move(limits)), source(std::move(source)) {
  }

  /**
   * @brief Populate visible editor entries from a validated shared archive.
   * @param zip Shared validated source archive.
   * @return Success or an editor index-limit failure.
   */
  [[nodiscard]] std::expected<void, Error> load(ZipArchive zip);
  /**
   * @brief Validate and stage one file/directory mutation before publishing table and budget
   * changes.
   * @param action Requested add, replace, directory, or recovery action.
   * @param name Case-sensitive archive entry name with slash separators.
   * @param bytes Payload copied during this call when no file import is requested.
   * @param import Optional path to a regular file whose bytes are staged immediately.
   * @param options Operation-specific parsing, emission, or entry policy.
   * @return Success or a structured edit error leaving logical contents unchanged.
   */
  [[nodiscard]] std::expected<void, Error> edit(
      Edit action, std::string_view name, std::span<const std::byte> bytes,
      const std::filesystem::path* import, EntryOptions options);
  /**
   * @brief Check cancellation, signed-JAR policy, and JMOD layout before output begins.
   * @param path Source or destination filesystem path.
   * @return Success or a format/signature/cancellation error.
   */
  [[nodiscard]] std::expected<void, Error> validate_write(const std::filesystem::path& path) const;
  /**
   * @brief Append matching visible-entry and staged-content records with name-index rollback on
   * failure.
   * @param entry Archive entry metadata transferred into the editor.
   * @param content Staged entry payload and options.
   */
  void append_entry(Entry entry, StagedEntry content);

  Format format; ///< Explicit JAR or JMOD output format.
  Limits limits; ///< Configured resource ceilings.
  std::filesystem::path source; ///< Original archive pathname used for diagnostics; empty for in-memory/create workflows.
  std::optional<ZipArchive> original; ///< Pinned original archive, absent when created from an empty editor.
  std::vector<Entry> entries; ///< Visible archive directory in stable editor order, including staged entries.
  std::vector<StagedEntry> contents; ///< Staged payload/option records aligned with visible archive entries.
  std::unordered_map<std::string, std::size_t, classpath::detail::NameHash, std::equal_to<>> names; ///< Entry-name to visible/indexed entry position lookup.
  std::size_t index_bytes = 0; ///< Accounted archive index storage in bytes.
  std::size_t staged_bytes = 0; ///< Total owned uncompressed payload bytes currently staged.
  bool opened_signed = false; ///< Whether the original JAR contained signature artifacts.
  bool edited = false; ///< Whether visible archive contents differ from the opened input.
};

void Archive::State::append_entry(Entry entry, StagedEntry content) {
  const auto size = entries.size();
  if (size == std::min(entries.capacity(), contents.capacity())) {
    const auto capacity = size + std::min(std::max<std::size_t>(size, 1), limits.max_archive_entries - size);
    entries.reserve(capacity);
    contents.reserve(capacity);
  }
  names.emplace(entry.name, size);
  index_bytes += EDITOR_ENTRY_OVERHEAD + 2 * entry.name.size();
  entries.push_back(std::move(entry));
  contents.push_back(std::move(content));
}

std::expected<void, Error> Archive::State::load(ZipArchive zip) {
  original.emplace(std::move(zip));
  const auto indexed = original->entries();
  // Budget the editor's entries and lookup before allocating its index.
  if (indexed.size() > limits.max_archive_index_bytes / EDITOR_ENTRY_OVERHEAD)
    return failure(ErrorKind::LimitExceeded, source, {}, "archive editor index exceeds its configured size");
  entries.reserve(indexed.size());
  contents.reserve(indexed.size());
  names.reserve(indexed.size());
  for (const auto& entry : indexed) {
    if (limits.cancellation.stop_requested())
      return failure(ErrorKind::Cancelled, source, {}, "archive open cancelled");
    if (EDITOR_ENTRY_OVERHEAD + 2 * entry.name.size() > limits.max_archive_index_bytes - index_bytes)
      return failure(ErrorKind::LimitExceeded, source, entry.name, "archive editor index exceeds its configured size");
    append_entry({entry.name, entry.uncompressed_size, entry.directory}, {&entry, {}, {}});
  }
  opened_signed = format == Format::Jar && std::ranges::any_of(entries, signature_entry);
  return {};
}

std::expected<void, Error> Archive::State::edit(
    Edit action, std::string_view name, std::span<const std::byte> bytes,
    const std::filesystem::path* import, EntryOptions options) {
  const auto error = [&](ErrorKind kind, std::string_view reason) { return failure(kind, source, name, reason); };
  if (limits.cancellation.stop_requested())
    return error(ErrorKind::Cancelled, "archive edit cancelled");
  if (name.size() > std::min<std::size_t>(limits.max_name_bytes, UINT16_MAX))
    return failure(ErrorKind::LimitExceeded, source, {}, "entry name exceeds its configured size or ZIP limit");
  const bool directory = action == Edit::Directory;
  if (!safe_entry_name(name, limits.max_path_depth) ||
      (action != Edit::Replace && (directory != name.ends_with('/') || !valid_utf8(name))))
    return error(ErrorKind::InvalidName, "expected a safe relative archive entry name");
  if (options.unix_permissions && *options.unix_permissions > 0777u)
    return error(ErrorKind::InvalidOptions, "Unix permissions must fit in the low nine mode bits");

  const auto match = names.find(name);
  if (action == Edit::Replace) {
    if (match == names.end())
      return error(ErrorKind::EntryNotFound, "archive entry does not exist");
    if (entries[match->second].directory)
      return error(ErrorKind::EntryIsDirectory, "archive entry is a directory");
  } else {
    if (match != names.end())
      return error(ErrorKind::EntryExists, "archive entry already exists");
    if (entries.size() >= limits.max_archive_entries ||
        EDITOR_ENTRY_OVERHEAD + 2 * name.size() > limits.max_archive_index_bytes - index_bytes)
      return error(ErrorKind::LimitExceeded, "archive editor index exceeds its configured limits");
  }
  auto* previous = action == Edit::Replace ? &contents[match->second] : nullptr;
  const auto released = previous && previous->bytes ? previous->bytes->size() : 0;
  const auto available = std::min(limits.max_entry_uncompressed_bytes, limits.max_staged_bytes - (staged_bytes - released));

  Bytes staged;
  if (import) {
    if (!valid_file_path(*import))
      return failure(ErrorKind::IoError, *import, name, "invalid entry input file path");
    const auto input = open_regular_file(*import);
    if (!input)
      return failure(ErrorKind::IoError, *import, name, "entry input is not a readable regular file");
    if (static_cast<std::uint64_t>(input->size) > available)
      return failure(ErrorKind::LimitExceeded, *import, name, "entry input exceeds its staging budget");
    staged.resize(static_cast<std::size_t>(input->size));
    for (std::size_t pos = 0; pos < staged.size();) {
      if (limits.cancellation.stop_requested())
        return failure(ErrorKind::Cancelled, *import, name, "archive file import cancelled");
      const auto count = std::min<std::size_t>(staged.size() - pos, 64 * 1024);
      if (std::fread(staged.data() + pos, 1, count, input->file.get()) != count)
        return failure(ErrorKind::IoError, *import, name, "could not read the entry input file");
      pos += count;
    }
    if (std::fgetc(input->file.get()) != EOF || std::ferror(input->file.get()))
      return failure(ErrorKind::IoError, *import, name, "entry input size changed during import");
  } else {
    if (bytes.size() > available)
      return error(ErrorKind::LimitExceeded, "entry exceeds its staging budget");
    if (!bytes.empty())
      staged.assign(bytes.begin(), bytes.end());
  }
  if (limits.cancellation.stop_requested())
    return error(ErrorKind::Cancelled, "archive edit cancelled");

  const auto size = staged.size();
  if (previous) {
    previous->bytes = std::move(staged);
    if (options.compression)
      previous->options.compression = options.compression;
    if (options.unix_permissions)
      previous->options.unix_permissions = options.unix_permissions;
    entries[match->second].uncompressed_size = size;
  } else {
    append_entry({std::string(name), size, directory}, {nullptr, std::move(staged), options});
  }
  staged_bytes = staged_bytes - released + size;
  edited = true;
  return {};
}

std::expected<void, Error> Archive::State::validate_write(const std::filesystem::path& path) const {
  if (limits.cancellation.stop_requested())
    return failure(ErrorKind::Cancelled, path, {}, "archive write cancelled");
  if (opened_signed && edited) {
    const auto signature = std::ranges::find_if(entries, signature_entry);
    if (signature != entries.end())
      return failure(ErrorKind::SignedJar, path, signature->name, "remove the JAR signature entries before saving edits");
  }
  if (format == Format::Jmod) {
    constexpr std::string_view DESCRIPTOR = "classes/module-info.class";
    const auto descriptor = names.find(DESCRIPTOR);
    if (descriptor == names.end() || entries[descriptor->second].directory)
      return failure(ErrorKind::InvalidJmodLayout, path, DESCRIPTOR, "JMOD requires a module descriptor file");
    for (const auto& entry : entries) {
      if (!jmod_section(entry.name))
        return failure(ErrorKind::InvalidJmodLayout, path, entry.name, "entry is outside the recognized JMOD sections");
    }
  }
  return {};
}

Archive::Archive(std::unique_ptr<State> state) : state(std::move(state)) {
}

Archive::~Archive() = default;
Archive::Archive(Archive&&) noexcept = default;
Archive& Archive::operator=(Archive&&) noexcept = default;

Archive Archive::create(Format format, Limits limits) {
  return Archive(std::make_unique<State>(format, std::move(limits)));
}

std::expected<Archive, Error> Archive::open(const std::filesystem::path& path, Format format, Limits limits) {
  auto zip = ZipArchive::open(path, source_kind(format), reader_limits(limits));
  if (!zip)
    return std::unexpected(archive_error(std::move(zip.error())));
  auto state = std::make_unique<State>(format, std::move(limits), path);
  if (auto loaded = state->load(std::move(*zip)); !loaded)
    return std::unexpected(std::move(loaded.error()));
  return Archive(std::move(state));
}

std::expected<Archive, Error> Archive::open(std::vector<std::byte> bytes, Format format, Limits limits) {
  auto zip = ZipArchive::open(std::move(bytes), source_kind(format), reader_limits(limits));
  if (!zip)
    return std::unexpected(archive_error(std::move(zip.error())));
  auto state = std::make_unique<State>(format, std::move(limits));
  if (auto loaded = state->load(std::move(*zip)); !loaded)
    return std::unexpected(std::move(loaded.error()));
  return Archive(std::move(state));
}

Format Archive::format() const {
  return state->format;
}

std::span<const Entry> Archive::entries() const {
  return state->entries;
}

const Entry* Archive::find(std::string_view name) const {
  const auto match = state->names.find(name);
  return match == state->names.end() ? nullptr : &state->entries[match->second];
}

// Every editor entry has staged bytes or an original index; add/replace/erase preserve this invariant.
std::expected<Bytes, Error> Archive::read(std::string_view name) const {
  if (state->limits.cancellation.stop_requested())
    return failure(ErrorKind::Cancelled, state->source, name, "archive read cancelled");
  const auto match = state->names.find(name);
  if (match == state->names.end())
    return failure(ErrorKind::EntryNotFound, state->source, name, "archive entry does not exist");
  if (state->entries[match->second].directory)
    return failure(ErrorKind::EntryIsDirectory, state->source, name, "archive entry is a directory");
  const auto& contents = state->contents[match->second];
  if (contents.bytes)
    return *contents.bytes;
  auto result = state->original->read(name, state->limits.max_entry_uncompressed_bytes); // NOLINT(bugprone-unchecked-optional-access)
  if (!result)
    return std::unexpected(archive_error(std::move(result.error())));
  return std::move(*result);
}

std::expected<void, Error> Archive::add(std::string_view name, std::span<const std::byte> bytes, EntryOptions options) {
  return state->edit(Edit::Add, name, bytes, nullptr, options);
}

std::expected<void, Error> Archive::replace(std::string_view name, std::span<const std::byte> bytes, EntryOptions options) {
  return state->edit(Edit::Replace, name, bytes, nullptr, options);
}

std::expected<void, Error> Archive::add_file(
    std::string_view name, const std::filesystem::path& path, EntryOptions options) {
  return state->edit(Edit::Add, name, {}, &path, options);
}

std::expected<void, Error> Archive::replace_file(
    std::string_view name, const std::filesystem::path& path, EntryOptions options) {
  return state->edit(Edit::Replace, name, {}, &path, options);
}

std::expected<void, Error> Archive::add_directory(std::string_view name, std::optional<std::uint16_t> unix_permissions) {
  return state->edit(Edit::Directory, name, {}, nullptr, {Compression::Store, unix_permissions});
}

std::expected<void, Error> Archive::erase(std::string_view name) {
  if (state->limits.cancellation.stop_requested())
    return failure(ErrorKind::Cancelled, state->source, name, "archive edit cancelled");
  const auto match = state->names.find(name);
  if (match == state->names.end())
    return failure(ErrorKind::EntryNotFound, state->source, name, "archive entry does not exist");
  const auto index = match->second;
  state->index_bytes -= EDITOR_ENTRY_OVERHEAD + 2 * state->entries[index].name.size();
  if (const auto& bytes = state->contents[index].bytes)
    state->staged_bytes -= bytes->size();
  state->names.erase(match);
  state->entries.erase(state->entries.begin() + static_cast<std::ptrdiff_t>(index));
  state->contents.erase(state->contents.begin() + static_cast<std::ptrdiff_t>(index));
  for (auto i = index; i < state->entries.size(); ++i)
    state->names.find(state->entries[i].name)->second = i;
  state->edited = true;
  return {};
}

std::expected<Bytes, Error> Archive::write() const {
  if (auto valid = state->validate_write(state->source); !valid)
    return std::unexpected(std::move(valid.error()));
  Bytes bytes;
  auto limits = state->limits;
  limits.max_archive_bytes = std::min<std::uint64_t>(limits.max_archive_bytes, bytes.max_size());
  const auto output = [&](std::span<const std::byte> chunk) {
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    return true;
  };
  const auto* original = state->original ? &*state->original : nullptr;
  auto written = write_zip(state->format, limits, original, state->entries, state->contents, state->source, output);
  if (!written)
    return std::unexpected(std::move(written.error()));
  return bytes;
}

std::expected<void, Error> Archive::save(const std::filesystem::path& path, SaveMode mode) const {
  if (auto valid = state->validate_write(path); !valid)
    return valid;
  if (mode == SaveMode::FailIfExists && valid_file_path(path)) {
    std::error_code status_error;
    if (std::filesystem::exists(std::filesystem::symlink_status(path, status_error)))
      return failure(ErrorKind::DestinationExists, path, {}, "archive destination already exists");
  }
  TemporaryArchiveFile file;
  if (auto opened = file.open(path); !opened)
    return opened;
  const auto output = [&](std::span<const std::byte> chunk) {
    return std::fwrite(chunk.data(), 1, chunk.size(), file.stream()) == chunk.size();
  };
  const auto* original = state->original ? &*state->original : nullptr;
  auto written = write_zip(state->format, state->limits, original, state->entries, state->contents, path, output);
  if (!written)
    return written;
  return file.publish(mode, state->limits.cancellation);
}

} // namespace cafe::archive
