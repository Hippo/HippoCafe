set(seed_dir "${CMAKE_CURRENT_BINARY_DIR}/seeds")
file(MAKE_DIRECTORY "${seed_dir}")
file(GLOB seeds CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/tests/fixtures/generated/*.class"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/historical/*.class"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/supplied/classes/*.class"
)
foreach(seed IN LISTS seeds)
  get_filename_component(seed_name "${seed}" NAME)
  configure_file("${seed}" "${seed_dir}/${seed_name}" COPYONLY)
endforeach()

# FuzzTest builds Abseil as C++17, which changes the ABI of its source location
# type. Keep registration in that mode and compile the properties as C++23.
add_library(hippocafe_fuzz_properties STATIC properties.cpp)
target_link_libraries(hippocafe_fuzz_properties PRIVATE hippocafe::hippocafe)
target_compile_options(hippocafe_fuzz_properties PRIVATE -fsanitize-coverage=pc-table)
hippocafe_configure_target(hippocafe_fuzz_properties)

add_executable(hippocafe_fuzz class_reader_fuzz.cpp roundtrip_fuzz.cpp)
set_target_properties(hippocafe_fuzz PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(hippocafe_fuzz PRIVATE hippocafe_fuzz_properties)
target_compile_definitions(hippocafe_fuzz PRIVATE HIPPOCAFE_FUZZ_SEED_DIR="${seed_dir}")
link_fuzztest(hippocafe_fuzz)
hippocafe_configure_target(hippocafe_fuzz)

add_executable(hippocafe_libfuzzer libfuzzer.cpp)
target_compile_features(hippocafe_libfuzzer PRIVATE cxx_std_17)
target_link_libraries(hippocafe_libfuzzer PRIVATE hippocafe_fuzz_properties)
target_compile_options(hippocafe_libfuzzer PRIVATE -fsanitize=fuzzer-no-link)
target_link_options(hippocafe_libfuzzer PRIVATE -fsanitize=fuzzer)
hippocafe_configure_target(hippocafe_libfuzzer)
