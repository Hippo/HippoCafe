#include <string>

#include <fuzztest/fuzztest.h>

#include "properties.hpp"

namespace {

void read_bytes(const std::string& input) {
  cafe::fuzz::exercise_reader(input);
}

FUZZ_TEST(ClassReaderFuzz, read_bytes)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_FUZZ_SEED_DIR));

} // namespace
