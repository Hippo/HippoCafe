#pragma once

#include <cafe/archive/archive.hpp>

#include "zip_archive.hpp"

namespace cafe::archive::detail {

struct StagedEntry {
  const ZipEntry* original = nullptr;
  std::optional<std::vector<std::byte>> bytes;
  EntryOptions options;
};

[[nodiscard]] Error archive_error(const classpath::ResolutionError& error);

[[nodiscard]] std::expected<void, Error> write_zip(
    Format format, const Limits& limits, const ZipArchive* original,
    std::span<const Entry> entries, std::span<const StagedEntry> contents,
    const std::filesystem::path& path, const ByteSink& sink);

} // namespace cafe::archive::detail
