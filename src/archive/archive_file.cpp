/**
 * @file
 * @brief Bounded ZIP/JAR/JMOD access and transactional archive editing.
 * @ingroup implementation
 */

#include "archive_file.hpp"

#include <cerrno>
#include <cstdlib>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cafe::archive::detail {

bool valid_file_path(const std::filesystem::path& path) {
  return !path.empty() &&
         path.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}

std::optional<RegularFile> open_regular_file(const std::filesystem::path& path) {
#ifdef _WIN32
  const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return std::nullopt;
  BY_HANDLE_FILE_INFORMATION metadata;
  LARGE_INTEGER length;
  if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &metadata) ||
      (metadata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      !GetFileSizeEx(handle, &length) || length.QuadPart < 0) {
    CloseHandle(handle);
    return std::nullopt;
  }
  const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_RDONLY | _O_BINARY);
  if (fd < 0) {
    CloseHandle(handle);
    return std::nullopt;
  }
  File file(_fdopen(fd, "rb"));
  if (!file) {
    _close(fd);
    return std::nullopt;
  }
  return RegularFile{std::move(file), length.QuadPart};
#else
  // Make the regular-file check against the opened descriptor. Nonblocking
  // prevents a substituted FIFO from stalling an archive registration.
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    return std::nullopt;
  struct stat metadata{};
  if (::fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_size < 0 || static_cast<std::uintmax_t>(metadata.st_size) > INT64_MAX) {
    ::close(fd);
    return std::nullopt;
  }
  File file(::fdopen(fd, "rb"));
  if (!file) {
    ::close(fd);
    return std::nullopt;
  }
  return RegularFile{std::move(file), static_cast<std::int64_t>(metadata.st_size)};
#endif
}

namespace {

/**
 * @brief Attach a destination path to a native archive-publication failure.
 * @param path Source or destination filesystem path.
 * @param detail Explanation copied into the structured error.
 * @return An unexpected archive I/O error.
 * @ingroup implementation
 */
std::unexpected<Error> io_error(const std::filesystem::path& path, std::string detail) {
  return std::unexpected(Error{ErrorKind::IoError, path, {}, std::move(detail)});
}

} // namespace

TemporaryArchiveFile::~TemporaryArchiveFile() {
  file.reset();
#ifdef _WIN32
  if (rename_handle)
    CloseHandle(rename_handle);
#endif
  if (!temporary.empty()) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  }
}

std::expected<void, Error> TemporaryArchiveFile::open(const std::filesystem::path& path) {
  if (!valid_file_path(path) || !path.has_filename())
    return io_error(path, "invalid archive destination path");
  std::error_code error;
  destination = std::filesystem::absolute(path, error);
  if (error)
    return io_error(path, error.message());
  const auto parent = destination.parent_path();

#ifdef _WIN32
  static std::atomic<unsigned long long> serial{0};
  HANDLE handle = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 32; ++attempt) {
    auto candidate = parent / (L".hippocafe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(serial++));
    handle = CreateFileW(candidate.c_str(), GENERIC_WRITE | DELETE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
      temporary = std::move(candidate);
      break;
    }
    if (GetLastError() != ERROR_FILE_EXISTS)
      break;
  }
  if (handle == INVALID_HANDLE_VALUE)
    return io_error(path, "could not create a temporary archive beside the destination");
  if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &rename_handle, DELETE, FALSE, 0)) {
    CloseHandle(handle);
    return io_error(path, "could not retain the temporary archive's rename handle");
  }
  const auto fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_WRONLY | _O_BINARY);
  if (fd < 0) {
    CloseHandle(handle);
    return io_error(path, "could not open the temporary archive for writing");
  }
  file.reset(_fdopen(fd, "wb"));
  if (!file)
    _close(fd);
#else
  auto pattern = (parent / ".hippocafe-XXXXXX").native();
  const auto fd = ::mkstemp(pattern.data());
  if (fd < 0)
    return io_error(path, std::generic_category().message(errno));
  temporary = std::move(pattern);
  file.reset(::fdopen(fd, "wb"));
  if (!file)
    ::close(fd);
#endif
  if (!file)
    return io_error(path, "could not open the temporary archive stream");
  return {};
}

std::FILE* TemporaryArchiveFile::stream() const {
  return file.get();
}

std::expected<void, Error> TemporaryArchiveFile::publish(SaveMode mode, const std::stop_token& cancellation) {
  if (std::fclose(file.release()) != 0)
    return io_error(destination, "could not close the temporary archive");
  if (cancellation.stop_requested())
    return std::unexpected(Error{ErrorKind::Cancelled, destination, {}, "archive save cancelled"});

#ifdef _WIN32
  const auto& name = destination.native();
  const auto name_bytes = name.size() * sizeof(wchar_t);
  std::vector<std::byte> buffer(sizeof(FILE_RENAME_INFO) + name_bytes);
  auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
  info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(name_bytes);
  std::memcpy(buffer.data() + offsetof(FILE_RENAME_INFO, FileName), name.data(), name_bytes);
  const auto rename = [&](FILE_INFO_BY_HANDLE_CLASS kind) {
    return SetFileInformationByHandle(rename_handle, kind, info, static_cast<DWORD>(buffer.size()));
  };
  if (mode == SaveMode::ReplaceExisting) {
    // FileRenameInfoEx overlays ReplaceIfExists with its 32-bit flags field.
    constexpr DWORD REPLACE_WITH_OPEN_HANDLES = 0x1 | 0x2;
    std::memcpy(buffer.data(), &REPLACE_WITH_OPEN_HANDLES, sizeof(REPLACE_WITH_OPEN_HANDLES));
    if (rename(FileRenameInfoEx)) {
      temporary.clear();
      return {};
    }
    const auto error = GetLastError();
    if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED && error != ERROR_INVALID_FUNCTION)
      return io_error(destination, std::system_category().message(static_cast<int>(error)));
    info->ReplaceIfExists = TRUE;
  }
  if (!rename(FileRenameInfo)) {
    const auto error = GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
      return std::unexpected(Error{ErrorKind::DestinationExists, destination, {}, "archive destination already exists"});
    return io_error(destination, std::system_category().message(static_cast<int>(error)));
  }
  temporary.clear();
#else
  if (mode == SaveMode::ReplaceExisting) {
    if (::rename(temporary.c_str(), destination.c_str()) != 0)
      return io_error(destination, std::generic_category().message(errno));
    temporary.clear();
  } else if (::link(temporary.c_str(), destination.c_str()) != 0) {
    const auto error = errno;
    if (error == EEXIST)
      return std::unexpected(Error{ErrorKind::DestinationExists, destination, {}, "archive destination already exists"});
    return io_error(destination, std::generic_category().message(error));
  }
#endif
  return {};
}

} // namespace cafe::archive::detail
