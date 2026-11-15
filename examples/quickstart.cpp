/**
 * @file
 * @brief Runnable read/write and archive-editing snippets.
 */

/** @cond example_programs */
// [read-write]
#include <cstddef>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hippo/cafe.hpp>

std::expected<cafe::WriteResult, std::string> roundtrip(std::vector<std::byte> bytes) {
  auto parsed = cafe::read_class(std::move(bytes));
  if (!parsed)
    return std::unexpected(std::move(parsed.error().detail));
  for (const auto& diagnostic : parsed->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';

  auto written = cafe::write_class(parsed->class_file);
  if (!written)
    return std::unexpected(std::move(written.error().detail));
  return std::move(*written);
}
// [read-write]

// [archive-edit]
std::expected<void, std::string> rewrite_entry(cafe::archive::Archive& archive, std::string_view name) {
  auto input = archive.read(name);
  if (!input)
    return std::unexpected(std::move(input.error().detail));
  auto output = roundtrip(std::move(*input));
  if (!output)
    return std::unexpected(std::move(output.error()));
  for (const auto& diagnostic : output->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  if (auto replaced = archive.replace(name, output->bytes); !replaced)
    return std::unexpected(std::move(replaced.error().detail));
  return {};
}
// [archive-edit]

int main() {
  auto file = cafe::ClassFile::create({52, 0}, "Example");
  if (!file)
    return 1;
  auto bytes = cafe::write_class(*file);
  if (!bytes)
    return 1;
  auto result = roundtrip(std::move(bytes->bytes));
  if (!result) {
    std::cerr << result.error() << '\n';
    return 1;
  }
  for (const auto& diagnostic : result->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  auto archive = cafe::archive::Archive::create(cafe::archive::Format::Jar);
  if (!archive.add("Example.class", result->bytes) || !rewrite_entry(archive, "Example.class"))
    return 1;
  return archive.write() ? 0 : 1;
}

/** @endcond */
