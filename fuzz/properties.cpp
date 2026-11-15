#include "properties.hpp"

#include <cstdlib>
#include <span>

#include <hippo/cafe.hpp>

namespace cafe::fuzz {
namespace {

ReadOptions read_options() {
  ReadOptions options;
  options.limits.max_input_bytes = MAX_INPUT_BYTES;
  options.limits.max_total_allocated_bytes = 16 * 1024 * 1024;
  options.limits.max_attributes = 10'000;
  options.limits.max_diagnostics = 64;
  options.allow_opaque_code = true;
  return options;
}

} // namespace

void exercise_reader(std::string_view input) {
  const auto bytes = std::as_bytes(std::span(input));
  auto options = read_options();
  (void)read_class(bytes, options);
  options.mode = ParseMode::Strict;
  (void)read_class(bytes, options);
}

void exercise_roundtrip(std::string_view input) {
  const auto bytes = std::as_bytes(std::span(input));
  auto options = read_options();
  const auto parsed = read_class(bytes, options);
  if (!parsed) {
    return;
  }

  options.mode = parsed->diagnostics.empty() ? ParseMode::Strict : ParseMode::Compatible;
  options.allow_opaque_code = false;
  for (const auto force_reencode : {false, true}) {
    const auto written = write_class(parsed->class_file, WriteOptions{.force_reencode = force_reencode});
    if (!written) {
      continue;
    }
    if (parsed->class_file.validity() == ClassValidity::OpaqueEssentialContent) {
      std::abort();
    }
    const auto reread = read_class(written->bytes, options);
    if (!reread) {
      std::abort();
    }
    const auto canonical = write_class(reread->class_file);
    if (!canonical || canonical->bytes != written->bytes) {
      std::abort();
    }
  }
}

} // namespace cafe::fuzz
