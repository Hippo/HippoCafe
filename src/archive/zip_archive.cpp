/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#include "zip_archive.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <variant>

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>

#include "archive_file.hpp"
#include "classpath/classpath_detail.hpp"
#include "zip_format.hpp"

namespace cafe::archive {
namespace {

using classpath::ClassPathLimits;
using classpath::ResolutionError;
using classpath::ResolutionErrorKind;
using classpath::SourceKind;
using classpath::SourceProvenance;
using classpath::detail::fail;

using namespace detail;

constexpr std::uint32_t DESCRIPTOR_MAGIC = 0x08074b50; ///< Optional ZIP trailing data-descriptor signature.

/**
 * @brief Seekable read-only minizip adapter over a pinned regular file or owned bytes. The logical
 * origin skips the validated JMOD wrapper; visible size can exclude the ZIP comment. Callback
 * access is serialized by ZipArchive state.
 * @ingroup implementation
 *
 * Minizip sees a seekable read-only logical ZIP. For JMOD the stream's
 * origin follows the validated JMOD header, so offsets need no correction.
 */
struct ZipStream : mz_stream {
  /**
   * @brief Initialize seekable read-only minizip adapter over a pinned regular file or owned bytes
   * @param file Class whose current declaration metadata is copied.
   * @param size Requested byte count or checked input byte length.
   * @ingroup implementation
   */
  ZipStream(File file, std::int64_t size) :
      mz_stream{&callbacks, nullptr}, input(std::move(file)), physical_end(size) {
  }
  /**
   * @brief Initialize seekable read-only minizip adapter over a pinned regular file or owned bytes
   * @param bytes Archive buffer transferred into this stream.
   * @ingroup implementation
   */
  explicit ZipStream(std::vector<std::byte> bytes) :
      mz_stream{&callbacks, nullptr}, input(std::move(bytes)),
      physical_end(static_cast<std::int64_t>(std::get<1>(input).size())) {
  }

  /**
   * @brief Read the pinned input's physical byte length.
   * @return The input size in bytes.
   * @ingroup implementation
   */
  [[nodiscard]] std::int64_t size() const {
    return physical_end - origin;
  }

  /**
   * @brief Read the logical ZIP length after wrapper/comment adjustments.
   * @return The byte length exposed to minizip.
   * @ingroup implementation
   */
  [[nodiscard]] std::int64_t visible_size() const {
    return std::min(size(), visible_end);
  }

  /**
   * @brief Read an exact bounded byte range without relying on a shared FILE cursor position.
   * @param offset Byte offset into the supplied source/output range.
   * @param out Caller-owned output container or decoded value destination.
   * @return True on complete read, false for bounds or I/O failure.
   * @ingroup implementation
   */
  [[nodiscard]] bool read_at(std::uint64_t offset, std::span<std::byte> out) {
    const auto end = static_cast<std::uint64_t>(size());
    if (offset > end || out.size() > end - offset)
      return false;
    if (out.empty())
      return true;
    const auto physical = static_cast<std::int64_t>(offset) + origin;
    if (auto* file = std::get_if<0>(&input)) {
      if (file_position != physical) {
        file_position = -1;
        std::clearerr(file->get());
#ifdef _WIN32
        if (_fseeki64(file->get(), physical, SEEK_SET) != 0)
#else
        if (::fseeko(file->get(), static_cast<off_t>(physical), SEEK_SET) != 0)
#endif
          return false;
      }
      if (std::fread(out.data(), 1, out.size(), file->get()) != out.size()) {
        file_position = -1;
        return false;
      }
      file_position = physical + static_cast<std::int64_t>(out.size());
    } else {
      std::memcpy(out.data(), std::get<1>(input).data() + physical, out.size());
    }
    return true;
  }

  /**
   * @brief Owned regular-file handle or in-memory archive bytes.
   */
  std::variant<File, std::vector<std::byte>> input;
  std::int64_t physical_end; ///< Exclusive physical input byte boundary.
  std::int64_t visible_end = INT64_MAX; ///< Exclusive logical ZIP boundary exposed to minizip, excluding comments when indexed.
  std::int64_t origin = 0; ///< Byte origin used to translate logical offsets to the original input.
  std::int64_t cursor = 0; ///< Current byte position within the bounded logical input.
  std::int64_t file_position = -1; ///< Cached FILE position used to avoid unnecessary seeks.

  static mz_stream_vtbl callbacks; ///< Static minizip stream callback table for this adapter.
};

/**
 * @brief Recover the ZIP stream adapter from minizip's callback pointer.
 * @param stream Native or minizip callback stream pointer.
 * @return A borrowed adapter reference valid during the callback.
 * @ingroup implementation
 */
ZipStream& self(void* stream) {
  return *static_cast<ZipStream*>(static_cast<mz_stream*>(stream));
}

mz_stream_vtbl ZipStream::callbacks = {
    .open = nullptr,
    .is_open = [](void*) -> std::int32_t { return MZ_OK; },
    .read = [](void* stream, void* buffer, std::int32_t count) -> std::int32_t {
      auto& input = self(stream);
      if (count < 0)
        return MZ_PARAM_ERROR;
      const auto bytes = std::min<std::int64_t>(count, input.visible_size() - input.cursor);
      if (!input.read_at(input.cursor, {static_cast<std::byte*>(buffer), static_cast<std::size_t>(bytes)}))
        return MZ_READ_ERROR;
      input.cursor += bytes;
      return static_cast<std::int32_t>(bytes);
    },
    .write = nullptr,
    .tell = [](void* stream) { return self(stream).cursor; },
    .seek = [](void* stream, std::int64_t offset, std::int32_t origin) -> std::int32_t {
      auto& input = self(stream);
      const auto base = origin == MZ_SEEK_SET ? 0 : origin == MZ_SEEK_CUR ? input.cursor
                                                                          : input.visible_size();
      if ((origin != MZ_SEEK_SET && origin != MZ_SEEK_CUR && origin != MZ_SEEK_END) ||
          offset < -base || offset > input.visible_size() - base)
        return MZ_SEEK_ERROR;
      input.cursor = base + offset;
      return MZ_OK;
    },
    .close = [](void*) -> std::int32_t { return MZ_OK; },
    .error = [](void*) -> std::int32_t { return MZ_OK; },
    .create = nullptr,
    .destroy = nullptr,
    .get_prop_int64 = nullptr,
    .set_prop_int64 = nullptr,
};

/**
 * @brief Validated central-directory byte extent, entry count, and comment-independent logical
 * end.
 * @ingroup implementation
 */
struct DirectoryBounds {
  std::uint64_t offset; ///< Logical ZIP byte offset of the central directory.
  std::uint64_t size; ///< Central-directory length in bytes.
  std::uint64_t entries; ///< Number of central-directory entries, including explicit directories.
  std::uint64_t logical_end; ///< Exclusive logical ZIP boundary before the archive comment.
};

/**
 * @brief Validate the declared single-disk classic/ZIP64 directory before opening minizip.
 * @param input Borrowed archive input adapter used for bounded reads.
 * @param path Source or destination filesystem path.
 * @return Directory bounds excluding any archive comment, or a structured archive error.
 * @ingroup implementation
 *
 * Confirm the declared single-disk directory before handing it to minizip's
 * record reader. This excludes its offset correction and recovery paths.
 */
std::expected<DirectoryBounds, ResolutionError> directory_bounds(ZipStream& input, const std::filesystem::path& path) {
  const auto corrupt = [&] {
    return fail(ResolutionErrorKind::ArchiveCorrupt, path, "invalid ZIP central directory footer");
  };
  const auto unsupported = [&] {
    return fail(ResolutionErrorKind::UnsupportedArchiveFeature, path, "split ZIP archives are unsupported");
  };
  const auto size = static_cast<std::uint64_t>(input.size());
  if (size < EOCD_SIZE)
    return corrupt();
  const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, EOCD_SIZE + UINT16_MAX));
  const auto tail_offset = size - tail_size;
  std::vector<std::byte> tail(tail_size);
  if (!input.read_at(tail_offset, tail))
    return corrupt();

  std::size_t end = tail.size() - EOCD_SIZE;
  while (little_endian<std::uint32_t>(tail, end) != EOCD_MAGIC ||
         little_endian<std::uint16_t>(tail, end + 20) != tail.size() - end - EOCD_SIZE) {
    if (end == 0)
      return corrupt();
    --end;
  }
  if (little_endian<std::uint16_t>(tail, end + 4) != 0 || little_endian<std::uint16_t>(tail, end + 6) != 0)
    return unsupported();

  const auto disk_count = little_endian<std::uint16_t>(tail, end + 8);
  DirectoryBounds bounds{little_endian<std::uint32_t>(tail, end + 16),
                         little_endian<std::uint32_t>(tail, end + 12),
                         little_endian<std::uint16_t>(tail, end + 10),
                         tail_offset + end + EOCD_SIZE};
  if (disk_count != bounds.entries)
    return corrupt();

  std::uint64_t directory_end = tail_offset + end;
  std::array<std::byte, 20> locator;
  if (directory_end >= locator.size() && input.read_at(directory_end - locator.size(), locator) &&
      little_endian<std::uint32_t>(locator, 0) == ZIP64_LOCATOR_MAGIC) {
    if (little_endian<std::uint32_t>(locator, 4) != 0 || little_endian<std::uint32_t>(locator, 16) != 1)
      return unsupported();
    const auto locator_offset = directory_end - locator.size();
    directory_end = little_endian<std::uint64_t>(locator, 8);
    std::array<std::byte, 56> record;
    if (directory_end > locator_offset || locator_offset - directory_end < record.size() ||
        !input.read_at(directory_end, record) || little_endian<std::uint32_t>(record, 0) != ZIP64_EOCD_MAGIC ||
        little_endian<std::uint64_t>(record, 4) != locator_offset - directory_end - 12)
      return corrupt();
    if (little_endian<std::uint32_t>(record, 16) != 0 || little_endian<std::uint32_t>(record, 20) != 0)
      return unsupported();

    const DirectoryBounds wide{little_endian<std::uint64_t>(record, 48),
                               little_endian<std::uint64_t>(record, 40),
                               little_endian<std::uint64_t>(record, 32),
                               bounds.logical_end};
    if (little_endian<std::uint64_t>(record, 24) != wide.entries ||
        (bounds.offset != UINT32_MAX && bounds.offset != wide.offset) ||
        (bounds.size != UINT32_MAX && bounds.size != wide.size) ||
        (bounds.entries != UINT16_MAX && bounds.entries != wide.entries))
      return corrupt();
    bounds = wide;
  }
  if (bounds.offset > directory_end || bounds.size != directory_end - bounds.offset ||
      bounds.entries > bounds.size / CENTRAL_HEADER_SIZE || (bounds.entries == 0 && bounds.size != 0))
    return corrupt();
  return bounds;
}

/**
 * @brief Bounded ZIP extra-field scan result retaining the optional ZIP64 payload view.
 * @ingroup implementation
 */
struct ExtraFields {
  bool valid = false; ///< Whether all extra-field records have valid bounds and supported structure.
  std::optional<std::span<const std::byte>> zip64; ///< Borrowed ZIP64 extra-field payload, when present.
};

/**
 * @brief Walk length-bounded ZIP extra fields and locate ZIP64 metadata.
 * @param input Bounded extra-field bytes; returned views borrow this storage.
 * @return A view describing validity and the optional ZIP64 payload.
 * @ingroup implementation
 */
ExtraFields extra_fields(std::span<const std::byte> input) {
  ExtraFields result;
  while (!input.empty()) {
    if (input.size() < 4)
      return result;
    const auto tag = little_endian<std::uint16_t>(input, 0);
    const auto size = little_endian<std::uint16_t>(input, 2);
    input = input.subspan(4);
    if (size > input.size())
      return result;
    if (tag == MZ_ZIP_EXTENSION_ZIP64) {
      if (result.zip64)
        return result;
      result.zip64 = input.first(size);
    }
    input = input.subspan(size);
  }
  result.valid = true;
  return result;
}

/**
 * @brief Check optional signed/unsigned ZIP data descriptors against central-directory values.
 * @param input Borrowed archive input adapter used for bounded reads.
 * @param offset Byte offset into the supplied source/output range.
 * @param next Exclusive boundary supplied by the next physical ZIP record.
 * @param entry Validated central-directory entry belonging to the pinned archive.
 * @param wide Whether the selected encoding uses extended operand widths.
 * @return True when one legal descriptor representation matches.
 * @ingroup implementation
 */
bool descriptor_valid(ZipStream& input, std::uint64_t offset, std::uint64_t next, const ZipEntry& entry, bool wide) {
  std::array<std::byte, 24> bytes;
  const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size(), next - offset));
  if (!input.read_at(offset, std::span(bytes).first(size)))
    return false;
  const auto size_without_signature = wide ? 20u : 12u;
  // The signature is optional and may coincide with the CRC of unsigned
  // descriptors. Test both lengths against the central directory's values.
  for (const auto skip : {4u, 0u}) {
    if (size < size_without_signature + skip ||
        (skip != 0 && little_endian<std::uint32_t>(bytes, 0) != DESCRIPTOR_MAGIC) ||
        little_endian<std::uint32_t>(bytes, skip) != entry.crc)
      continue;
    const auto compressed = wide ? little_endian<std::uint64_t>(bytes, skip + 4)
                                 : little_endian<std::uint32_t>(bytes, skip + 4);
    const auto uncompressed = wide ? little_endian<std::uint64_t>(bytes, skip + 12)
                                   : little_endian<std::uint32_t>(bytes, skip + 8);
    if (compressed == entry.compressed_size && uncompressed == entry.uncompressed_size)
      return true;
  }
  return false;
}

/**
 * @brief Validate a local header and payload extent against its central entry and physical
 * successor.
 * @param input Borrowed archive input adapter used for bounded reads.
 * @param entry Validated central-directory entry belonging to the pinned archive.
 * @param next Exclusive boundary supplied by the next physical ZIP record.
 * @param scratch Reusable byte buffer overwritten with the local filename and extra fields.
 * @return True when the complete local record is bounded and consistent.
 * @ingroup implementation
 *
 * Validate every local header and its physical extent without inflating
 * data. The next local header (or the directory) is a hard upper boundary.
 */
bool local_record_valid(ZipStream& input, const ZipEntry& entry, std::uint64_t next,
                        std::vector<std::byte>& scratch) {
  std::array<std::byte, LOCAL_HEADER_SIZE> header;
  const auto at = entry.local_offset;
  if (at > next || next - at < header.size() || !input.read_at(at, header) ||
      little_endian<std::uint32_t>(header, 0) != LOCAL_HEADER_MAGIC ||
      little_endian<std::uint16_t>(header, 6) != entry.flags ||
      little_endian<std::uint16_t>(header, 8) != entry.method ||
      little_endian<std::uint16_t>(header, 26) != entry.name.size())
    return false;
  const auto extra_size = little_endian<std::uint16_t>(header, 28);
  const auto header_size = header.size() + entry.name.size() + extra_size;
  if (header_size > next - at || entry.compressed_size > next - at - header_size)
    return false;

  scratch.resize(entry.name.size() + extra_size);
  if (!input.read_at(at + header.size(), scratch) ||
      !std::ranges::equal(std::as_bytes(std::span(entry.name)), std::span(scratch).first(entry.name.size())))
    return false;
  const auto extra = extra_fields(std::span(scratch).subspan(entry.name.size()));
  if (!extra.valid)
    return false;
  if ((entry.flags & MZ_ZIP_FLAG_DATA_DESCRIPTOR) != 0)
    return descriptor_valid(input, at + header_size + entry.compressed_size, next, entry, extra.zip64.has_value());

  if (little_endian<std::uint32_t>(header, 14) != entry.crc)
    return false;
  std::uint64_t compressed = little_endian<std::uint32_t>(header, 18);
  std::uint64_t uncompressed = little_endian<std::uint32_t>(header, 22);
  if (compressed == UINT32_MAX || uncompressed == UINT32_MAX) {
    const auto required = 8u * ((compressed == UINT32_MAX) + (uncompressed == UINT32_MAX));
    if (!extra.zip64 || extra.zip64->size() < required)
      return false;
    std::size_t offset = 0;
    if (uncompressed == UINT32_MAX) {
      uncompressed = little_endian<std::uint64_t>(*extra.zip64, offset);
      offset += 8;
    }
    if (compressed == UINT32_MAX)
      compressed = little_endian<std::uint64_t>(*extra.zip64, offset);
  }
  return compressed == entry.compressed_size && uncompressed == entry.uncompressed_size;
}

/**
 * @brief RAII deleter for the minizip ZIP handle.
 * @ingroup implementation
 */
struct ZipDeleter {
  /**
   * @brief Close and release the owned native stream/handle through its matching API.
   * @param handle Native file/stream handle transferred to the owner.
   * @ingroup implementation
   */
  void operator()(void* handle) const noexcept {
    if (handle) {
      mz_zip_close(handle);
      mz_zip_delete(&handle);
    }
  }
};

} // namespace

bool detail::safe_entry_name(std::string_view name, std::size_t max_depth) {
  if (name.empty() || name.find('\0') != std::string_view::npos ||
      name.find_first_of("\\:") != std::string_view::npos)
    return false;
  while (!name.empty()) {
    const auto slash = name.find('/');
    const auto component = name.substr(0, slash);
    if (max_depth == 0 || component.empty() || component == "." || component == "..")
      return false;
    --max_depth;
    if (slash == std::string_view::npos)
      break;
    name.remove_prefix(slash + 1);
  }
  return true;
}

/**
 * @brief Shared archive input, validated directory index, retained ZIP metadata, and a mutex
 * protecting the mutable minizip cursor.
 * @ingroup implementation
 */
struct ZipArchive::State {
  /**
   * @brief Initialize shared archive input, validated directory index, retained ZIP metadata, and a
   * mutex protecting the mutable minizip cursor.
   * @param input Owned archive input adapter.
   * @param path Source or destination filesystem path.
   * @param kind Payload or error discriminator.
   * @param limits Resource ceilings and cooperative cancellation policy.
   */
  State(ZipStream input, std::filesystem::path path, SourceKind kind, ClassPathLimits limits) :
      input(std::move(input)), path(std::move(path)), kind(kind), limits(std::move(limits)) {
  }

  /**
   * @brief Validate directory/local records and build the immutable name index.
   * @return Success or a framing, unsupported-feature, path, or resource error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> index();
  /**
   * @brief Stream one entry through checksum/length/limit validation, optionally retaining output.
   * @param entry Validated central-directory entry belonging to the pinned archive.
   * @param byte_limit Maximum uncompressed bytes permitted for this read.
   * @param result Destination for decompressed bytes; null validates and discards the payload.
   * @return Success or a corruption, compression, I/O, budget, or cancellation error.
   */
  [[nodiscard]] std::expected<void, ResolutionError> decode(
      const ZipEntry& entry, std::size_t byte_limit, std::vector<std::byte>* result);

  /**
   * @brief Owned input stream backing the retained archive index.
   */
  ZipStream input;
  std::filesystem::path path; ///< Owned filesystem path identifying this source.
  SourceKind kind; ///< Discriminator selecting the interpretation of this record.
  ClassPathLimits limits; ///< Configured resource ceilings.
  std::unique_ptr<void, ZipDeleter> handle; ///< Owned minizip reader handle borrowing this state's input adapter.
  std::vector<ZipEntry> entries; ///< Validated central-directory records owned by the immutable index.
  std::unordered_map<std::string_view, std::size_t, classpath::detail::NameHash, std::equal_to<>> names; ///< Entry-name to visible/indexed entry position lookup.
  std::size_t index_bytes = 0; ///< Accounted archive index storage in bytes.
  std::mutex mutex; ///< Serializes shared mutable cache or stream state.
};

std::expected<void, ResolutionError> ZipArchive::State::index() {
  const auto corrupt = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::ArchiveCorrupt, path, reason);
  };
  const auto cancelled = [&] {
    return fail(ResolutionErrorKind::Cancelled, path, "archive indexing cancelled");
  };
  if (limits.cancellation.stop_requested())
    return cancelled();
  if (static_cast<std::uint64_t>(input.size()) > limits.max_archive_bytes)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "archive exceeds its configured file size");
  if (kind == SourceKind::Jmod) {
    std::array<std::byte, JMOD_MAGIC.size()> header;
    if (!input.read_at(0, header) || header[0] != JMOD_MAGIC[0] || header[1] != JMOD_MAGIC[1])
      return corrupt("missing JMOD header");
    if (header != JMOD_MAGIC)
      return fail(ResolutionErrorKind::UnsupportedArchiveFeature, path, "unsupported JMOD format version");
    input.origin = JMOD_MAGIC.size();
  }

  auto bounds = directory_bounds(input, path);
  if (!bounds)
    return std::unexpected(std::move(bounds.error()));
  // The original comment length was checked above. Hide the comment from
  // minizip's reverse signature search, which also matches bytes in comments.
  input.visible_end = static_cast<std::int64_t>(bounds->logical_end);
  constexpr std::size_t ENTRY_OVERHEAD = sizeof(ZipEntry) + 64;
  if (bounds->entries > limits.max_archive_entries ||
      bounds->entries > limits.max_archive_index_bytes / ENTRY_OVERHEAD)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "archive central directory exceeds its configured index limits");
  if (bounds->entries == 0)
    return {};

  handle.reset(mz_zip_create());
  if (!handle)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "unable to allocate ZIP reader");
  void* zip = handle.get();
  mz_zip_set_recover(zip, 0);
  std::uint64_t declared_count = 0;
  if (mz_zip_open(zip, static_cast<mz_stream*>(&input), MZ_OPEN_MODE_READ) != MZ_OK ||
      mz_zip_get_number_entry(zip, &declared_count) != MZ_OK || declared_count != bounds->entries)
    return corrupt("minizip rejected the ZIP central directory");

  entries.reserve(static_cast<std::size_t>(declared_count));
  names.reserve(static_cast<std::size_t>(declared_count));
  index_bytes = static_cast<std::size_t>(declared_count) * ENTRY_OVERHEAD;
  const auto end = bounds->offset + bounds->size;
  std::vector<std::byte> scratch;
  for (auto at = bounds->offset; at < end;) {
    if (limits.cancellation.stop_requested())
      return cancelled();
    if (entries.size() >= declared_count || end - at < CENTRAL_HEADER_SIZE)
      return corrupt("central directory entry count or length mismatch");

    std::array<std::byte, CENTRAL_HEADER_SIZE> header;
    if (!input.read_at(at, header) || little_endian<std::uint32_t>(header, 0) != CENTRAL_HEADER_MAGIC)
      return corrupt("invalid central directory entry header");
    const auto name_size = little_endian<std::uint16_t>(header, 28);
    const auto extra_size = little_endian<std::uint16_t>(header, 30);
    const auto comment_size = little_endian<std::uint16_t>(header, 32);
    const auto record_size = CENTRAL_HEADER_SIZE + name_size + extra_size + comment_size;
    if (record_size > end - at)
      return corrupt("central directory entry exceeds its bounds");
    if (name_size > limits.max_name_bytes || name_size > limits.max_archive_index_bytes - index_bytes)
      return fail(ResolutionErrorKind::ResourceLimitExceeded, path, "archive entry names exceed the configured index limits");
    scratch.resize(extra_size);
    if (!input.read_at(at + CENTRAL_HEADER_SIZE + name_size, scratch))
      return corrupt("could not read central directory extra fields");
    const auto extra = extra_fields(scratch);
    const unsigned zip64_size = 8u * ((little_endian<std::uint32_t>(header, 20) == UINT32_MAX) +
                                      (little_endian<std::uint32_t>(header, 24) == UINT32_MAX) +
                                      (little_endian<std::uint32_t>(header, 42) == UINT32_MAX)) +
                                4u * (little_endian<std::uint16_t>(header, 34) == UINT16_MAX);
    if (!extra.valid || (zip64_size != 0 && (!extra.zip64 || extra.zip64->size() < zip64_size)))
      return corrupt("invalid central directory extra fields");

    mz_zip_file* info = nullptr;
    if (mz_zip_goto_entry(zip, static_cast<std::int64_t>(at)) != MZ_OK || mz_zip_entry_get_info(zip, &info) != MZ_OK ||
        info->filename_size != name_size || info->extrafield_size != extra_size ||
        info->comment_size != comment_size || info->compressed_size < 0 ||
        info->uncompressed_size < 0 || info->disk_offset < 0)
      return corrupt("invalid central directory entry");
    if (info->disk_number != 0)
      return fail(ResolutionErrorKind::UnsupportedArchiveFeature, path, "split ZIP entries are unsupported");

    std::string name(info->filename, name_size);
    if (!safe_entry_name(name, limits.max_path_depth))
      return corrupt("unsafe ZIP entry path");
    if (mz_zip_entry_is_symlink(zip) == MZ_OK)
      return fail(ResolutionErrorKind::UnsupportedArchiveFeature, path, "ZIP symlinks are unsupported");
    const auto host = MZ_HOST_SYSTEM(info->version_madeby);
    const auto file_type = (info->external_fa >> 16) & 0170000u;
    if ((host == MZ_HOST_SYSTEM_UNIX || host == MZ_HOST_SYSTEM_OSX_DARWIN) &&
        file_type != 0 && file_type != 0100000 && file_type != 0040000)
      return fail(ResolutionErrorKind::UnsupportedArchiveFeature, path, "non-regular ZIP entries are unsupported");

    entries.push_back({std::move(name), static_cast<std::int64_t>(at), static_cast<std::uint64_t>(info->disk_offset),
                       static_cast<std::uint64_t>(info->compressed_size), static_cast<std::uint64_t>(info->uncompressed_size),
                       info->crc, info->compression_method, info->flag, mz_zip_entry_is_dir(zip) == MZ_OK});
    if (!names.emplace(entries.back().name, entries.size() - 1).second)
      return corrupt("duplicate ZIP entry name");
    index_bytes += name_size;
    at += record_size;
  }
  if (entries.size() != declared_count)
    return corrupt("central directory entry count mismatch");

  std::vector<std::size_t> local_order(entries.size());
  std::iota(local_order.begin(), local_order.end(), std::size_t{0});
  std::ranges::sort(local_order, {}, [this](auto index) { return entries[index].local_offset; });
  for (std::size_t index = 0; index < local_order.size(); ++index) {
    if (limits.cancellation.stop_requested())
      return cancelled();
    const auto& entry = entries[local_order[index]];
    const auto next = index + 1 < local_order.size() ? entries[local_order[index + 1]].local_offset : bounds->offset;
    if (next > bounds->offset || !local_record_valid(input, entry, next, scratch))
      return fail(ResolutionErrorKind::ArchiveCorrupt, {}, {kind, path, entry.name, {}},
                  "inconsistent or overlapping local ZIP record");
  }
  return {};
}

ZipArchive::ZipArchive(std::shared_ptr<State> state) : state(std::move(state)) {
}

bool ZipEntry::is_deflated() const {
  return method == MZ_COMPRESS_METHOD_DEFLATE;
}

std::expected<ZipArchive, classpath::ResolutionError> ZipArchive::open(
    const std::filesystem::path& path, classpath::SourceKind kind, classpath::ClassPathLimits limits) {
  if (path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
    return fail(ResolutionErrorKind::UnsafePath, path, "archive path contains NUL");
  auto file = open_regular_file(path);
  if (!file)
    return fail(ResolutionErrorKind::IoError, path, "archive source is not a readable regular file");
  auto state = std::make_shared<State>(ZipStream(std::move(file->file), file->size), path, kind, std::move(limits));
  if (auto indexed = state->index(); !indexed)
    return std::unexpected(std::move(indexed.error()));
  return ZipArchive(std::move(state));
}

std::expected<ZipArchive, classpath::ResolutionError> ZipArchive::open(
    std::vector<std::byte> bytes, classpath::SourceKind kind, classpath::ClassPathLimits limits) {
  if (bytes.size() > INT64_MAX)
    return fail(ResolutionErrorKind::ResourceLimitExceeded, {}, "archive size cannot be represented");
  auto state = std::make_shared<State>(ZipStream(std::move(bytes)), std::filesystem::path{}, kind, std::move(limits));
  if (auto indexed = state->index(); !indexed)
    return std::unexpected(std::move(indexed.error()));
  return ZipArchive(std::move(state));
}

std::span<const ZipEntry> ZipArchive::entries() const {
  return state->entries;
}

const ZipEntry* ZipArchive::find(std::string_view name) const {
  const auto match = state->names.find(name);
  return match == state->names.end() ? nullptr : &state->entries[match->second];
}

std::size_t ZipArchive::index_size() const {
  return state->index_bytes;
}

std::expected<std::vector<std::byte>, ResolutionError> ZipArchive::read(std::string_view name, std::size_t byte_limit) const {
  const SourceProvenance source{state->kind, state->path, std::string(name), {}};
  const auto entry = find(name);
  if (!entry || entry->directory)
    return fail(ResolutionErrorKind::NotFound, {}, source, "ZIP entry is absent");
  std::vector<std::byte> result;
  if (auto decoded = state->decode(*entry, byte_limit, &result); !decoded)
    return std::unexpected(std::move(decoded.error()));
  return result;
}

std::expected<void, ResolutionError> ZipArchive::verify(const ZipEntry& entry) const {
  return state->decode(entry, state->limits.max_entry_uncompressed_bytes, nullptr);
}

std::expected<void, ResolutionError> ZipArchive::State::decode(
    const ZipEntry& entry, std::size_t byte_limit, std::vector<std::byte>* result) {
  const SourceProvenance source{kind, path, entry.name, {}};
  const auto corrupt = [&](std::string_view reason) {
    return fail(ResolutionErrorKind::ArchiveCorrupt, {}, source, reason);
  };
  const auto cancelled = [&] {
    return fail(ResolutionErrorKind::Cancelled, {}, source, "archive extraction cancelled");
  };
  constexpr std::uint16_t SUPPORTED_FLAGS = MZ_ZIP_FLAG_DATA_DESCRIPTOR | MZ_ZIP_FLAG_UTF8 | MZ_ZIP_FLAG_DEFLATE_SUPER_FAST;
  if ((entry.flags & ~SUPPORTED_FLAGS) != 0)
    return fail(ResolutionErrorKind::UnsupportedArchiveFeature, {}, source, "encrypted or unsupported ZIP flags");
  if (entry.method != MZ_COMPRESS_METHOD_STORE && entry.method != MZ_COMPRESS_METHOD_DEFLATE)
    return fail(ResolutionErrorKind::UnsupportedCompression, {}, source, "ZIP entry must be stored or deflated");
  if ((entry.method == MZ_COMPRESS_METHOD_STORE && entry.compressed_size != entry.uncompressed_size) ||
      (entry.method == MZ_COMPRESS_METHOD_DEFLATE && entry.compressed_size == 0))
    return corrupt("invalid compressed size for ZIP entry");

  const auto size = entry.uncompressed_size;
  const auto compressed = entry.compressed_size;
  const auto ratio = compressed == 0 ? 0 : (size / compressed + (size % compressed != 0));
  if (compressed > limits.max_entry_compressed_bytes ||
      size > std::min(limits.max_entry_uncompressed_bytes, byte_limit) || ratio > limits.max_compression_ratio)
    return fail(ResolutionErrorKind::EntryTooLarge, {}, source, "ZIP entry exceeds its byte or compression ratio limit");
  if (limits.cancellation.stop_requested())
    return cancelled();

  // Allocate before an entry is open so allocation failure also leaves the
  // shared ZIP handle ready for a later lookup.
  if (result)
    result->resize(static_cast<std::size_t>(size));
  std::scoped_lock guard(mutex);
  void* zip = handle.get();
  mz_zip_file* current = nullptr;
  if (mz_zip_goto_entry(zip, entry.directory_offset) != MZ_OK || mz_zip_entry_get_info(zip, &current) != MZ_OK ||
      current->filename_size != entry.name.size() || std::string_view(current->filename, current->filename_size) != entry.name ||
      current->compression_method != entry.method || current->flag != entry.flags ||
      current->crc != entry.crc || current->disk_offset != static_cast<std::int64_t>(entry.local_offset) ||
      current->compressed_size != static_cast<std::int64_t>(entry.compressed_size) ||
      current->uncompressed_size != static_cast<std::int64_t>(entry.uncompressed_size))
    return corrupt("ZIP entry changed since indexing");
  if (mz_zip_entry_read_open(zip, 0, nullptr) != MZ_OK)
    return corrupt("could not open ZIP entry");

  std::array<std::byte, 64 * 1024> scratch;
  std::size_t offset = 0;
  while (offset < size && !limits.cancellation.stop_requested()) {
    const auto request = static_cast<std::int32_t>(std::min<std::uint64_t>(scratch.size(), size - offset));
    auto* target = result ? result->data() + offset : scratch.data();
    const auto got = mz_zip_entry_read(zip, target, request);
    if (got <= 0 || got > request)
      break;
    offset += static_cast<std::size_t>(got);
  }
  std::byte extra;
  const bool done = offset == size && mz_zip_entry_read(zip, &extra, 1) == 0;
  void* stream = nullptr;
  std::int64_t used = -1;
  const bool compressed_size_matches = mz_zip_entry_get_compress_stream(zip, &stream) == MZ_OK &&
                                       mz_stream_get_prop_int64(stream, MZ_STREAM_PROP_TOTAL_IN, &used) == MZ_OK &&
                                       used == static_cast<std::int64_t>(compressed);
  const auto closed = mz_zip_entry_read_close(zip, nullptr, nullptr, nullptr);
  if (limits.cancellation.stop_requested())
    return cancelled();
  if (!done || !compressed_size_matches || closed != MZ_OK)
    return corrupt("ZIP entry length, compressed stream or CRC does not match its index");
  return {};
}

std::expected<ZipMetadata, ResolutionError> ZipArchive::metadata(const ZipEntry& entry) const {
  std::scoped_lock guard(state->mutex);
  auto& input = state->input;
  const auto central_offset = static_cast<std::uint64_t>(entry.directory_offset);
  std::array<std::byte, CENTRAL_HEADER_SIZE> central;
  std::array<std::byte, LOCAL_HEADER_SIZE> local;
  const auto corrupt = [&] {
    return fail(ResolutionErrorKind::ArchiveCorrupt, {}, {state->kind, state->path, entry.name, {}},
                "could not read indexed ZIP metadata");
  };
  if (!input.read_at(central_offset, central) || !input.read_at(entry.local_offset, local) ||
      little_endian<std::uint32_t>(central, 0) != CENTRAL_HEADER_MAGIC ||
      little_endian<std::uint32_t>(local, 0) != LOCAL_HEADER_MAGIC ||
      little_endian<std::uint16_t>(central, 28) != entry.name.size() ||
      little_endian<std::uint16_t>(local, 26) != entry.name.size())
    return corrupt();

  const auto local_extra_size = little_endian<std::uint16_t>(local, 28);
  const auto central_extra_size = little_endian<std::uint16_t>(central, 30);
  const auto comment_size = little_endian<std::uint16_t>(central, 32);
  const auto local_extra_offset = entry.local_offset + local.size() + entry.name.size();
  const auto central_extra_offset = central_offset + central.size() + entry.name.size();
  ZipMetadata metadata{
      little_endian<std::uint16_t>(central, 4),
      little_endian<std::uint16_t>(central, 36),
      little_endian<std::uint32_t>(central, 38),
      little_endian<std::uint32_t>(local, 10),
      little_endian<std::uint32_t>(central, 12),
      local_extra_offset + local_extra_size,
      std::vector<std::byte>(local_extra_size),
      std::vector<std::byte>(central_extra_size),
      std::vector<std::byte>(comment_size),
  };
  if (!input.read_at(local_extra_offset, metadata.local_extra) ||
      !input.read_at(central_extra_offset, metadata.central_extra) ||
      !input.read_at(central_extra_offset + central_extra_size, metadata.comment) ||
      !extra_fields(metadata.local_extra).valid || !extra_fields(metadata.central_extra).valid)
    return corrupt();
  return metadata;
}

std::expected<std::vector<std::byte>, ResolutionError> ZipArchive::comment() const {
  std::scoped_lock guard(state->mutex);
  auto& input = state->input;
  std::vector<std::byte> result(static_cast<std::size_t>(input.size() - input.visible_end));
  if (!input.read_at(input.visible_end, result))
    return fail(ResolutionErrorKind::IoError, state->path, "could not read the ZIP archive comment");
  return result;
}

std::expected<bool, ResolutionError> ZipArchive::copy_compressed(
    const ZipEntry& entry, std::uint64_t offset, const ByteSink& sink) const {
  std::scoped_lock guard(state->mutex);
  std::array<std::byte, 64 * 1024> scratch;
  for (std::uint64_t left = entry.compressed_size; left != 0;) {
    if (state->limits.cancellation.stop_requested())
      return fail(ResolutionErrorKind::Cancelled, {}, {state->kind, state->path, entry.name, {}},
                  "archive copy cancelled");
    const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(left, scratch.size()));
    const auto chunk = std::span(scratch).first(size);
    if (!state->input.read_at(offset, chunk))
      return fail(ResolutionErrorKind::IoError, {}, {state->kind, state->path, entry.name, {}},
                  "could not read the indexed compressed payload");
    if (!sink(chunk))
      return false;
    offset += size;
    left -= size;
  }
  return true;
}

} // namespace cafe::archive
