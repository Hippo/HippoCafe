#include <fstream>
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

  std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
  if (!output || !output.write(reinterpret_cast<const char*>(written->bytes.data()),
                               static_cast<std::streamsize>(written->bytes.size()))) {
    std::cerr << "could not write output\n";
    return 1;
  }
  output.close();
  if (!output) {
    std::cerr << "could not close output\n";
    return 1;
  }
}
