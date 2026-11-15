#include <string>

#include <fuzztest/fuzztest.h>

#include "properties.hpp"

namespace {

void roundtrip_bytes(const std::string& input) {
  cafe::fuzz::exercise_roundtrip(input);
}

FUZZ_TEST(RoundtripFuzz, roundtrip_bytes)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_FUZZ_SEED_DIR));

} // namespace
