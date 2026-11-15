#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include <cafe/class_file.hpp>
#include <cafe/error.hpp>
#include <cafe/read_options.hpp>

namespace cafe {

struct ReadResult {
  ClassFile class_file;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::expected<ReadResult, ReadError> read_class(
    std::span<const std::byte> data, const ReadOptions& options = {});
[[nodiscard]] std::expected<ReadResult, ReadError> read_class(
    std::vector<std::byte>&& data, const ReadOptions& options = {});

} // namespace cafe
