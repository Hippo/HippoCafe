/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#pragma once

#include <cafe/archive/archive.hpp>

#include "zip_archive.hpp"

namespace cafe::archive::detail {

/**
 * @brief Optional owned replacement payload and entry options; absent bytes retain an original
 * archive entry.
 * @ingroup implementation
 */
struct StagedEntry {
  const ZipEntry* original = nullptr; ///< Borrowed original entry in the pinned archive index, or nullptr for a new entry.
  std::optional<std::vector<std::byte>> bytes; ///< Owned staged uncompressed bytes; empty means retain the original payload.
  EntryOptions options; ///< Owned compression and permission choices for the staged entry.
};

/**
 * @brief Translate shared archive-reader resolution errors into editor-domain errors.
 * @param source Resolution failure consumed into the archive error domain.
 * @return An owned archive Error preserving path, entry, and explanation.
 */
[[nodiscard]] Error archive_error(classpath::ResolutionError source);

/**
 * @brief Emit the visible editor state through a bounded byte sink.
 * @param format Explicit Jar or Jmod format.
 * @param limits Resource ceilings and cooperative cancellation policy.
 * @param original Pinned original archive for retained entry payloads, or nullptr for new output.
 * @param entries Ordered entries or metadata records for this operation.
 * @param contents Ordered staged payload records matching visible entries.
 * @param path Source or destination filesystem path.
 * @param sink Output callback; false stops emission without publishing partial output.
 * @return Success or a source-integrity/compression/budget/cancellation/sink error.
 */
[[nodiscard]] std::expected<void, Error> write_zip(
    Format format, const Limits& limits, const ZipArchive* original,
    std::span<const Entry> entries, std::span<const StagedEntry> contents,
    const std::filesystem::path& path, const ByteSink& sink);

} // namespace cafe::archive::detail
