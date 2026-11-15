#pragma once

#include <cstdio>
#include <memory>

#include <cafe/archive/archive.hpp>

namespace cafe::archive::detail {

struct CloseFile {
  void operator()(std::FILE* file) const noexcept {
    std::fclose(file);
  }
};
using File = std::unique_ptr<std::FILE, CloseFile>;

struct RegularFile {
  File file;
  std::int64_t size;
};

[[nodiscard]] bool valid_file_path(const std::filesystem::path& path);
[[nodiscard]] std::optional<RegularFile> open_regular_file(const std::filesystem::path& path);

class TemporaryArchiveFile {
public:
  TemporaryArchiveFile() = default;
  ~TemporaryArchiveFile();
  TemporaryArchiveFile(const TemporaryArchiveFile&) = delete;
  TemporaryArchiveFile& operator=(const TemporaryArchiveFile&) = delete;

  [[nodiscard]] std::expected<void, Error> open(const std::filesystem::path& path);
  [[nodiscard]] std::FILE* stream() const;
  [[nodiscard]] std::expected<void, Error> publish(SaveMode mode, std::stop_token cancellation);

private:
  std::filesystem::path destination;
  std::filesystem::path temporary;
  File file;
#ifdef _WIN32
  void* rename_handle = nullptr;
#endif
};

} // namespace cafe::archive::detail
