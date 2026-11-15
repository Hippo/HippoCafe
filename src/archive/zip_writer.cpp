#include "zip_writer.hpp"

#include <algorithm>
#include <numeric>

#include <mz.h>
#include <mz_crypt.h>
#include <mz_strm.h>
#include <mz_strm_zlib.h>

#include "zip_format.hpp"

namespace cafe::archive::detail {
namespace {

using Bytes = std::vector<std::byte>;
constexpr std::uint16_t STORED = MZ_COMPRESS_METHOD_STORE;
constexpr std::uint16_t DEFLATED = MZ_COMPRESS_METHOD_DEFLATE;
constexpr std::uint16_t UNIX_HOST = MZ_HOST_SYSTEM_UNIX << 8;
constexpr std::uint32_t DEFAULT_DOS_DATE = 0x00210000; // 1980-01-01 00:00
constexpr std::size_t CHUNK_BYTES = 64 * 1024;

// ZIP numeric fields are little-endian, including those in extra fields.
void append(Bytes& record, std::uint64_t number, unsigned width) {
  for (unsigned i = 0; i < width; ++i)
    record.push_back(static_cast<std::byte>(number >> (i * 8)));
}

void append(Bytes& record, std::span<const std::byte> bytes) {
  if (!bytes.empty())
    record.insert(record.end(), bytes.begin(), bytes.end());
}

void append_metadata_extras(Bytes& record, std::span<const std::byte> extras) {
  while (!extras.empty()) {
    const auto tag = little_endian<std::uint16_t>(extras, 0);
    const auto size = 4u + little_endian<std::uint16_t>(extras, 2);
    switch (tag) {
      case 0x000a: // NTFS timestamps
      case 0x000d: // PKWARE Unix timestamps and ownership
      case 0x5455: // Extended timestamps
      case 0x5855: // Info-ZIP Unix timestamps and ownership
      case 0x7855: // Info-ZIP Unix ownership
      case 0x7875: // Info-ZIP Unix ownership (new)
      case 0x6375: // Unicode comment
      case 0x7075: // Unicode path
      case 0xcafe: // Java JAR marker
        append(record, extras.first(size));
        break;
    }
    extras = extras.subspan(size);
  }
}

struct Record {
  std::uint64_t offset = 0;
  std::uint64_t compressed_size = 0;
  std::uint32_t crc = 0;
  std::uint16_t method = STORED;
  std::uint16_t flags = 0;
};

bool encode_header(Bytes& bytes, const Entry& entry, const Record& record, const ZipMetadata& metadata, bool central) {
  const bool wide_size = entry.uncompressed_size >= UINT32_MAX;
  const bool wide_compressed = record.compressed_size >= UINT32_MAX;
  const bool wide_offset = central && record.offset >= UINT32_MAX;
  const bool zip64 = wide_size || wide_compressed || wide_offset;
  const auto comment = central ? std::span<const std::byte>(metadata.comment) : std::span<const std::byte>{};

  bytes.clear();
  append(bytes, central ? CENTRAL_HEADER_MAGIC : LOCAL_HEADER_MAGIC, 4);
  if (central)
    append(bytes, metadata.made_by, 2);
  append(bytes, zip64 ? 45 : 20, 2);
  append(bytes, record.flags, 2);
  append(bytes, record.method, 2);
  append(bytes, central ? metadata.central_time : metadata.local_time, 4);
  append(bytes, record.crc, 4);
  // ZIP64 local headers put both sizes in the extra field.
  const bool local_wide = zip64 && !central;
  append(bytes, wide_compressed || local_wide ? UINT32_MAX : record.compressed_size, 4);
  append(bytes, wide_size || local_wide ? UINT32_MAX : entry.uncompressed_size, 4);
  append(bytes, entry.name.size(), 2);
  const auto extra_length_offset = bytes.size();
  append(bytes, 0, 2);
  if (central) {
    append(bytes, comment.size(), 2);
    append(bytes, 0, 2); // disk number
    append(bytes, metadata.internal_attributes, 2);
    append(bytes, metadata.external_attributes, 4);
    append(bytes, wide_offset ? UINT32_MAX : record.offset, 4);
  }
  append(bytes, std::as_bytes(std::span(entry.name)));

  const auto start_of_extras = bytes.size();
  if (zip64) {
    append(bytes, MZ_ZIP_EXTENSION_ZIP64, 2);
    append(bytes, local_wide ? 16 : 8u * (wide_size + wide_compressed + wide_offset), 2);
    if (wide_size || local_wide)
      append(bytes, entry.uncompressed_size, 8);
    if (wide_compressed || local_wide)
      append(bytes, record.compressed_size, 8);
    if (wide_offset)
      append(bytes, record.offset, 8);
  }
  append_metadata_extras(bytes, central ? metadata.central_extra : metadata.local_extra);
  const auto extra_size = bytes.size() - start_of_extras;
  if (extra_size > UINT16_MAX)
    return false;
  bytes[extra_length_offset] = static_cast<std::byte>(extra_size);
  bytes[extra_length_offset + 1] = static_cast<std::byte>(extra_size >> 8);
  append(bytes, comment);
  return true;
}

void encode_footer(Bytes& bytes, std::uint64_t offset, std::uint64_t size, std::size_t count,
                   std::span<const std::byte> comment) {
  bytes.clear();
  if (offset >= UINT32_MAX || size >= UINT32_MAX || count >= UINT16_MAX) {
    append(bytes, ZIP64_EOCD_MAGIC, 4);
    append(bytes, 44, 8); // remaining record length
    append(bytes, UNIX_HOST | 45, 2);
    append(bytes, 45, 2);
    append(bytes, 0, 8); // disk numbers
    append(bytes, count, 8);
    append(bytes, count, 8);
    append(bytes, size, 8);
    append(bytes, offset, 8);
    append(bytes, ZIP64_LOCATOR_MAGIC, 4);
    append(bytes, 0, 4);
    append(bytes, offset + size, 8);
    append(bytes, 1, 4);
  }
  append(bytes, EOCD_MAGIC, 4);
  append(bytes, 0, 4); // disk numbers
  append(bytes, std::min<std::size_t>(count, UINT16_MAX), 2);
  append(bytes, std::min<std::size_t>(count, UINT16_MAX), 2);
  append(bytes, std::min<std::uint64_t>(size, UINT32_MAX), 4);
  append(bytes, std::min<std::uint64_t>(offset, UINT32_MAX), 4);
  append(bytes, comment.size(), 2);
  append(bytes, comment);
}

struct CompressionBuffer : mz_stream {
  explicit CompressionBuffer(std::size_t limit) : mz_stream{&callbacks, nullptr}, limit(limit) {
  }

  Bytes bytes;
  std::size_t limit;
  bool exceeded = false;
  static mz_stream_vtbl callbacks;
};

mz_stream_vtbl CompressionBuffer::callbacks = [] {
  mz_stream_vtbl result{};
  result.is_open = [](void*) -> std::int32_t { return MZ_OK; };
  result.write = [](void* stream, const void* data, std::int32_t count) -> std::int32_t {
    auto& buffer = *static_cast<CompressionBuffer*>(static_cast<mz_stream*>(stream));
    if (count <= 0)
      return count;
    const auto size = static_cast<std::size_t>(count);
    if (buffer.exceeded || size > buffer.limit - buffer.bytes.size()) {
      buffer.exceeded = true;
      return MZ_WRITE_ERROR;
    }
    try {
      append(buffer.bytes, {static_cast<const std::byte*>(data), size});
    } catch (const std::bad_alloc&) {
      buffer.exceeded = true;
      return MZ_MEM_ERROR;
    }
    return count;
  };
  return result;
}();

struct DeflateDeleter {
  void operator()(void* stream) const noexcept {
    mz_stream_zlib_delete(&stream);
  }
};

std::expected<void, ErrorKind> deflate(
    std::span<const std::byte> input, const Limits& limits, CompressionBuffer& output) {
  std::unique_ptr<void, DeflateDeleter> stream(mz_stream_zlib_create());
  if (!stream)
    return std::unexpected(ErrorKind::LimitExceeded);
  if (mz_stream_set_base(stream.get(), static_cast<mz_stream*>(&output)) != MZ_OK ||
      mz_stream_set_prop_int64(stream.get(), MZ_STREAM_PROP_COMPRESS_LEVEL, 6) != MZ_OK ||
      mz_stream_open(stream.get(), nullptr, MZ_OPEN_MODE_WRITE) != MZ_OK)
    return std::unexpected(ErrorKind::IoError);

  while (!input.empty() && !limits.cancellation.stop_requested()) {
    const auto size = std::min(input.size(), CHUNK_BYTES);
    if (mz_stream_write(stream.get(), input.data(), static_cast<std::int32_t>(size)) != static_cast<std::int32_t>(size))
      break;
    input = input.subspan(size);
  }
  const auto closed = mz_stream_close(stream.get());
  if (limits.cancellation.stop_requested())
    return std::unexpected(ErrorKind::Cancelled);
  if (output.exceeded)
    return std::unexpected(ErrorKind::LimitExceeded);
  if (!input.empty() || closed != MZ_OK || output.bytes.empty())
    return std::unexpected(ErrorKind::IoError);
  return {};
}

bool entry_fits(std::uint64_t size, std::uint64_t compressed, const Limits& limits) {
  const auto ratio = compressed == 0 ? 0 : size / compressed + (size % compressed != 0);
  return compressed <= limits.max_entry_compressed_bytes && ratio <= limits.max_compression_ratio;
}

std::expected<ZipMetadata, Error> entry_metadata(const ZipArchive* archive, const StagedEntry& source, bool directory) {
  ZipMetadata metadata{UNIX_HOST | 20, 0, 0, DEFAULT_DOS_DATE, DEFAULT_DOS_DATE, 0, {}, {}, {}};
  const auto type = directory ? 0040000u : 0100000u;
  if (source.original) {
    auto original = archive->metadata(*source.original);
    if (!original)
      return std::unexpected(archive_error(original.error()));
    metadata = std::move(*original);
  } else {
    metadata.external_attributes = ((type | (directory ? 0755u : 0644u)) << 16) | (directory ? 0x10u : 0u);
  }
  if (source.options.unix_permissions) {
    if (MZ_HOST_SYSTEM(metadata.made_by) != MZ_HOST_SYSTEM_UNIX) {
      metadata.made_by = UNIX_HOST | (metadata.made_by & 0xffu);
      metadata.external_attributes = (metadata.external_attributes & 0xffffu) | (type << 16);
    }
    metadata.external_attributes &= ~(0777u << 16);
    metadata.external_attributes |= static_cast<std::uint32_t>(*source.options.unix_permissions) << 16;
  }
  return metadata;
}

std::vector<std::size_t> local_order(Format format, std::span<const Entry> entries,
                                     std::span<const StagedEntry> contents) {
  std::vector<std::size_t> order(entries.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  const auto offset = [&](std::size_t index) {
    const auto* original = contents[index].original;
    return original ? original->local_offset : UINT64_MAX;
  };
  std::ranges::stable_sort(order, {}, offset);
  if (format == Format::Jar) {
    const auto manifest = std::ranges::find_if(order, [&](std::size_t index) {
      return !contents[index].original && entries[index].name == "META-INF/MANIFEST.MF";
    });
    if (manifest != order.end())
      std::rotate(order.begin(), manifest, manifest + 1);
  }
  return order;
}

} // namespace

std::expected<void, Error> write_zip(
    Format format, const Limits& limits, const ZipArchive* original,
    std::span<const Entry> entries, std::span<const StagedEntry> contents,
    const std::filesystem::path& path, const ByteSink& sink) {
  const auto error = [&](ErrorKind kind, std::string_view name, std::string_view detail) {
    return std::unexpected(Error{kind, path, std::string(name), std::string(detail)});
  };
  std::uint64_t uncompressed = 0;
  for (const auto& entry : entries) {
    if (entry.uncompressed_size > limits.max_write_uncompressed_bytes - uncompressed)
      return error(ErrorKind::LimitExceeded, entry.name, "archive exceeds its uncompressed write budget");
    uncompressed += entry.uncompressed_size;
  }

  std::uint64_t written = 0;
  ErrorKind sink_error = ErrorKind::IoError;
  const ByteSink send = [&](std::span<const std::byte> bytes) {
    if (limits.cancellation.stop_requested()) {
      sink_error = ErrorKind::Cancelled;
      return false;
    }
    if (bytes.size() > limits.max_archive_bytes - written) {
      sink_error = ErrorKind::LimitExceeded;
      return false;
    }
    if (!bytes.empty() && !sink(bytes))
      return false;
    written += bytes.size();
    return true;
  };
  const auto output_error = [&](std::string_view name = {}) {
    switch (sink_error) {
      case ErrorKind::Cancelled:
        return error(sink_error, name, "archive write cancelled");
      case ErrorKind::LimitExceeded:
        return error(sink_error, name, "archive exceeds its configured output size");
      default:
        return error(sink_error, name, "could not write archive output");
    }
  };

  const auto origin = format == Format::Jmod ? JMOD_MAGIC.size() : 0;
  if (origin && !send(JMOD_MAGIC))
    return output_error();
  std::vector<Record> records(entries.size());
  Bytes header;
  for (const auto index : local_order(format, entries, contents)) {
    const auto& entry = entries[index];
    const auto& source = contents[index];
    auto& record = records[index];
    record.offset = written - origin;
    auto metadata = entry_metadata(original, source, entry.directory);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    record.flags = source.original ? source.original->flags & MZ_ZIP_FLAG_UTF8 : MZ_ZIP_FLAG_UTF8;
    CompressionBuffer compressed(limits.max_entry_compressed_bytes);
    std::span<const std::byte> payload;
    if (source.bytes) {
      payload = *source.bytes;
      const auto default_method = entry.directory || (source.original && source.original->method == STORED)
                                      ? Compression::Store
                                      : Compression::Deflate;
      if (source.options.compression.value_or(default_method) == Compression::Deflate) {
        const auto deflated = deflate(payload, limits, compressed);
        if (deflated && entry_fits(entry.uncompressed_size, compressed.bytes.size(), limits)) {
          record.method = DEFLATED;
        } else if (!deflated && deflated.error() == ErrorKind::Cancelled) {
          return error(ErrorKind::Cancelled, entry.name, "archive write cancelled");
        } else if (!deflated && deflated.error() != ErrorKind::LimitExceeded) {
          return error(deflated.error(), entry.name, "could not deflate ZIP entry");
        } else if (source.options.compression) {
          return error(ErrorKind::LimitExceeded, entry.name, "deflated entry exceeds its configured limits");
        }
      }
      for (auto left = payload; !left.empty();) {
        if (limits.cancellation.stop_requested())
          return error(ErrorKind::Cancelled, entry.name, "archive write cancelled");
        const auto size = std::min(left.size(), CHUNK_BYTES);
        record.crc = mz_crypt_crc32_update(record.crc, reinterpret_cast<const std::uint8_t*>(left.data()),
                                           static_cast<std::int32_t>(size));
        left = left.subspan(size);
      }
      if (record.method == DEFLATED)
        payload = compressed.bytes;
      record.compressed_size = payload.size();
      if (!entry_fits(entry.uncompressed_size, record.compressed_size, limits))
        return error(ErrorKind::LimitExceeded, entry.name, "entry exceeds its configured compressed size or ratio");
    } else {
      const auto& base = *source.original;
      if (auto verified = original->verify(base); !verified)
        return std::unexpected(archive_error(verified.error()));
      record.method = base.method;
      record.crc = base.crc;
      record.compressed_size = base.compressed_size;
      if (record.method == DEFLATED)
        record.flags |= base.flags & MZ_ZIP_FLAG_DEFLATE_SUPER_FAST;
    }
    if (!encode_header(header, entry, record, *metadata, false))
      return error(ErrorKind::LimitExceeded, entry.name, "local ZIP extra fields exceed the format limit");
    if (!send(header))
      return output_error(entry.name);
    if (source.bytes) {
      while (!payload.empty()) {
        const auto size = std::min(payload.size(), CHUNK_BYTES);
        if (!send(payload.first(size)))
          return output_error(entry.name);
        payload = payload.subspan(size);
      }
    } else {
      auto copied = original->copy_compressed(*source.original, metadata->data_offset, send);
      if (!copied)
        return std::unexpected(archive_error(copied.error()));
      if (!*copied)
        return output_error(entry.name);
    }
  }

  const auto directory_offset = written - origin;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    auto metadata = entry_metadata(original, contents[index], entry.directory);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    if (!encode_header(header, entry, records[index], *metadata, true))
      return error(ErrorKind::LimitExceeded, entry.name, "central ZIP extra fields exceed the format limit");
    if (!send(header))
      return output_error(entry.name);
  }
  Bytes comment;
  if (original) {
    auto loaded = original->comment();
    if (!loaded)
      return std::unexpected(archive_error(loaded.error()));
    comment = std::move(*loaded);
  }
  encode_footer(header, directory_offset, written - origin - directory_offset, entries.size(), comment);
  if (!send(header))
    return output_error();
  return {};
}

} // namespace cafe::archive::detail
