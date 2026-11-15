add_executable(hippocafe_benchmarks roundtrip_benchmarks.cpp)
target_link_libraries(hippocafe_benchmarks PRIVATE hippocafe::hippocafe)
target_compile_definitions(hippocafe_benchmarks PRIVATE
  HIPPOCAFE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/supplied/classes"
)
hippocafe_configure_target(hippocafe_benchmarks)
