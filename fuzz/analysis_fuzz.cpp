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

void rewrite_methods_through_ssa(const std::string& input) {
  cafe::fuzz::exercise_ssa(input);
}

void edit_ssa_program(const std::string& input) {
  cafe::fuzz::exercise_ssa_program(input);
}

FUZZ_TEST(AnalysisFuzz, analyze_methods)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_FUZZ_SEED_DIR));

FUZZ_TEST(AnalysisFuzz, index_archive)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_ARCHIVE_SEED_DIR));

FUZZ_TEST(AnalysisFuzz, rewrite_methods_through_ssa)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_INPUT_BYTES))
    .WithSeeds(fuzztest::ReadFilesFromDirectory(HIPPOCAFE_FUZZ_SEED_DIR));

FUZZ_TEST(AnalysisFuzz, edit_ssa_program)
    .WithDomains(fuzztest::Arbitrary<std::string>().WithMaxSize(cafe::fuzz::MAX_PROGRAM_BYTES))
    .WithSeeds({{""}, {"1234"}, {"loop-diamond"}});

} // namespace
