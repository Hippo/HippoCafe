#include <cstddef>
#include <cstdint>
#include <string_view>

#include "properties.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  cafe::fuzz::exercise_reader(bytes);
  cafe::fuzz::exercise_roundtrip(bytes);
  cafe::fuzz::exercise_analysis(bytes);
  cafe::fuzz::exercise_ssa(bytes);
  if (bytes.size() <= cafe::fuzz::MAX_PROGRAM_BYTES)
    cafe::fuzz::exercise_ssa_program(bytes);
  cafe::fuzz::exercise_archive(bytes);
  return 0;
}
