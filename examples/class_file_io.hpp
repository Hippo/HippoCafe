#pragma once

#include <cstddef>
#include <expected>
#include <fstream>
#include <string_view>
#include <vector>

#include <cafe/read_options.hpp>

namespace examples {

inline std::expected<std::vector<std::byte>, std::string_view> load_class(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return std::unexpected("could not open input");
  const auto size = file.tellg();
  if (size < 0 || static_cast<std::size_t>(size) > cafe::ReadLimits{}.max_input_bytes)
    return std::unexpected("input exceeds the class-file size limit");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    return std::unexpected("could not read input");
  return bytes;
}

} // namespace examples
