/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#pragma once

#include <cstdio>
#include <memory>

#include <cafe/archive/archive.hpp>

namespace cafe::archive::detail {

/**
 * @brief RAII FILE deleter; destruction closes the stream without publishing output.
 * @ingroup implementation
 */
struct CloseFile {
  /**
   * @brief Close and release the owned native stream/handle through its matching API.
   * @param file Class whose current declaration metadata is copied.
   */
  void operator()(std::FILE* file) const noexcept {
    std::fclose(file);
  }
};
/**
 * @brief Unique FILE ownership with an fclose deleter.
 */
using File = std::unique_ptr<std::FILE, CloseFile>;

/**
 * @brief Owning regular-file stream paired with its checked signed byte length.
 * @ingroup implementation
 */
struct RegularFile {
  File file; ///< Pinned native file/stream owner retained for safe reads or publication.
  std::int64_t size; ///< Checked byte length of the input or indexed range.
};

/**
 * @brief Reject embedded NUL and invalid native file path spellings.
 * @param path Source or destination filesystem path.
 * @return True when the path can safely reach file APIs.
 */
[[nodiscard]] bool valid_file_path(const std::filesystem::path& path);
/**
 * @brief Open a stable regular-file handle without blocking on special files.
 * @param path Source or destination filesystem path.
 * @return An owning FILE handle and signed byte length, or nullopt on failure.
 */
[[nodiscard]] std::optional<RegularFile> open_regular_file(const std::filesystem::path& path);

/**
 * @brief RAII temporary file for atomic archive publication. Destruction removes unpublished
 * temporary output. The destination is changed only after successful close and publication.
 * @ingroup implementation
 */
class TemporaryArchiveFile {
public:
  /**
   * @brief Initialize rAII temporary file for atomic archive publication
   */
  TemporaryArchiveFile() = default;
  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~TemporaryArchiveFile();
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  TemporaryArchiveFile(const TemporaryArchiveFile&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  TemporaryArchiveFile& operator=(const TemporaryArchiveFile&) = delete;

  /**
   * @brief Create a private temporary regular file beside the destination.
   * @param path Source or destination filesystem path.
   * @return Success or a native I/O failure; no destination is published yet.
   */
  [[nodiscard]] std::expected<void, Error> open(const std::filesystem::path& path);
  /**
   * @brief Borrow the opened temporary FILE stream.
   * @return A non-owning pointer valid until publication or destruction.
   */
  [[nodiscard]] std::FILE* stream() const;
  /**
   * @brief Close completed output and atomically publish the temporary file according to the save
   * policy.
   * @param mode Destination-existence policy; FailIfExists is the default.
   * @param cancellation Cooperative cancellation token checked before publication and during work.
   * @return Success or destination/I/O/cancellation error; unpublished output is cleaned up by
   *   destruction.
   */
  [[nodiscard]] std::expected<void, Error> publish(SaveMode mode, const std::stop_token& cancellation);

private:
  std::filesystem::path destination; ///< Final archive publication pathname.
  std::filesystem::path temporary; ///< Unpublished temporary output pathname removed on cleanup.
  File file; ///< Pinned native file/stream owner retained for safe reads or publication.
#ifdef _WIN32
  void* rename_handle = nullptr; ///< Windows handle retained for atomic rename after closing the write stream.
#endif
};

} // namespace cafe::archive::detail
