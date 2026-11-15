#pragma once

#include <cstddef>
#include <expected>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <cafe/read_options.hpp>

namespace examples {

inline std::expected<std::vector<std::byte>, std::string_view> load_class(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return std::unexpected("could not open input");
  const auto size = file.tellg();
  if (size < 0)
    return std::unexpected("could not determine input size");
  const auto length = static_cast<std::streamoff>(size);
  if (!std::in_range<std::size_t>(length) || !std::in_range<std::streamsize>(length) ||
      static_cast<std::size_t>(length) > cafe::ReadLimits{}.max_input_bytes)
    return std::unexpected("input exceeds the class-file size limit");
  std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  if (!file.seekg(0))
    return std::unexpected("could not seek input");
  if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length)))
    return std::unexpected("could not read input");
  return bytes;
}

inline std::expected<void, std::string_view> save_class(const char* path, std::span<const std::byte> bytes) {
  if (!std::in_range<std::streamsize>(bytes.size()))
    return std::unexpected("output exceeds the stream size limit");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || (!bytes.empty() && !output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))))
    return std::unexpected("could not write output");
  output.close();
  if (!output)
    return std::unexpected("could not close output");
  return {};
}

} // namespace examples
