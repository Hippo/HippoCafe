# The transitive versions match this FuzzTest release. Declaring the archives
# here gives them content hashes and enables local FETCHCONTENT_SOURCE_DIR overrides.
FetchContent_Declare(abseil-cpp
  URL https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/20260526.0
  URL_HASH SHA256=6e1aee535473414164bf83e4ebc40240dec71a4701f8a642d906e95bea1aea0c
  EXCLUDE_FROM_ALL SYSTEM
)
FetchContent_Declare(re2
  URL https://codeload.github.com/google/re2/tar.gz/refs/tags/2025-11-05
  URL_HASH SHA256=87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67
  EXCLUDE_FROM_ALL SYSTEM
)
FetchContent_Declare(antlr_cpp
  URL https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip
  URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
  EXCLUDE_FROM_ALL SYSTEM
)
FetchContent_Declare(fuzztest
  URL https://codeload.github.com/google/fuzztest/tar.gz/refs/tags/2026-06-29
  URL_HASH SHA256=f012eb72e85f0601e51495bcb1c9e9d14b92982f58e4643cfc08a534c4eca7a2
  EXCLUDE_FROM_ALL SYSTEM
)
set(FUZZTEST_FUZZING_MODE ON)
set(ANTLR_BUILD_CPP_TESTS OFF)
FetchContent_MakeAvailable(fuzztest)
fuzztest_setup_fuzzing_flags()
