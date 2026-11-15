#include <string>

#include <fuzztest/fuzztest.h>

#include "properties.hpp"

namespace {

void analyze_methods(const std::string& input) {
  cafe::fuzz::exercise_analysis(input);
}

void index_archive(const std::string& input) {
  cafe::fuzz::exercise_archive(input);
}

FUZZ_TEST(AnalysisFuzz, analyze_methods)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_FUZZ_SEED_DIR));

FUZZ_TEST(AnalysisFuzz, index_archive)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_ARCHIVE_SEED_DIR));

} // namespace
