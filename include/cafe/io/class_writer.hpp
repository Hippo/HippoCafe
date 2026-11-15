#pragma once

#include <cstddef>
#include <expected>
#include <vector>

#include <cafe/class_file.hpp>
#include <cafe/error.hpp>
#include <cafe/write_options.hpp>

namespace cafe {

struct WriteResult {
  std::vector<std::byte> bytes;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::expected<WriteResult, WriteError> write_class(
    const ClassFile& class_file, const WriteOptions& options = {});

} // namespace cafe
