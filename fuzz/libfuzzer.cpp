#include <cstddef>
#include <cstdint>
#include <string_view>

#include "properties.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  cafe::fuzz::exercise_reader(bytes);
  cafe::fuzz::exercise_roundtrip(bytes);
  return 0;
}
