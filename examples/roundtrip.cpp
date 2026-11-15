/**
 * @file
 * @brief Command-line class-file round trip with diagnostic reporting.
 */

/** @cond example_programs */
#include <iostream>
#include <utility>

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: hippocafe_roundtrip input.class output.class\n";
    return 1;
  }
  auto input = examples::load_class(argv[1]);
  if (!input) {
    std::cerr << input.error() << '\n';
    return 1;
  }
  const auto parsed = cafe::read_class(std::move(*input));
  if (!parsed) {
    std::cerr << parsed.error().path << " at byte " << parsed.error().offset << ": " << parsed.error().detail << '\n';
    return 1;
  }
  for (const auto& warning : parsed->diagnostics)
    std::cerr << warning.path << " at byte " << warning.offset << ": " << warning.detail << '\n';
  const auto written = cafe::write_class(parsed->class_file);
  if (!written) {
    std::cerr << written.error().path << ": " << written.error().detail << '\n';
    return 1;
  }
  for (const auto& warning : written->diagnostics)
    std::cerr << warning.path << " at byte " << warning.offset << ": " << warning.detail << '\n';

  if (auto saved = examples::save_class(argv[2], written->bytes); !saved) {
    std::cerr << argv[2] << ": " << saved.error() << '\n';
    return 1;
  }
}

/** @endcond */
