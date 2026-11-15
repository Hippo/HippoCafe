set(fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
file(GLOB test_sources CONFIGURE_DEPENDS unit/*.cpp integration/*.cpp regression/*.cpp)
add_executable(hippocafe_tests ${test_sources})
target_link_libraries(hippocafe_tests PRIVATE hippocafe::hippocafe GTest::gtest_main)
target_include_directories(hippocafe_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(hippocafe_tests PRIVATE HIPPOCAFE_FIXTURE_DIR="${fixture_dir}")
hippocafe_configure_target(hippocafe_tests)

add_test(NAME hippocafe.tests COMMAND hippocafe_tests)
add_test(NAME hippocafe.fixture_checksums COMMAND "${CMAKE_COMMAND}"
  "-DFIXTURE_DIR=${fixture_dir}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/verify_checksums.cmake"
)

find_package(Java 17 QUIET COMPONENTS Runtime Development)
if(Java_FOUND)
  set(java_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/java-fixtures")
  set(java_sources "${fixture_dir}/java/module-info.java" "${fixture_dir}/java/cafe/fixtures/Features.java")
  add_custom_command(OUTPUT "${java_fixture_dir}/module-info.class"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${java_fixture_dir}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -g -parameters -d "${java_fixture_dir}" ${java_sources}
    BYPRODUCTS
      "${java_fixture_dir}/cafe/fixtures/Features.class"
      "${java_fixture_dir}/cafe/fixtures/Features$Choice.class"
      "${java_fixture_dir}/cafe/fixtures/Features$Empty.class"
      "${java_fixture_dir}/cafe/fixtures/Features$Number.class"
      "${java_fixture_dir}/cafe/fixtures/Marker.class"
    DEPENDS ${java_sources}
    VERBATIM
  )
  add_custom_target(hippocafe_java_fixtures DEPENDS "${java_fixture_dir}/module-info.class")
  add_dependencies(hippocafe_tests hippocafe_java_fixtures)
  target_compile_definitions(hippocafe_tests PRIVATE HIPPOCAFE_JAVA_FIXTURE_DIR="${java_fixture_dir}")

  # The installed OpenJDK JAR is exercised in the build tree with no vendored copy.
  file(REAL_PATH "${Java_JAVAC_EXECUTABLE}" javac_executable)
  cmake_path(GET javac_executable PARENT_PATH java_bin_dir)
  set(jrt_fs_jar "${java_bin_dir}/../lib/jrt-fs.jar")
  if(EXISTS "${jrt_fs_jar}")
    set(jrt_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/jrt-fixtures")
    file(MAKE_DIRECTORY "${jrt_fixture_dir}")
    file(ARCHIVE_EXTRACT INPUT "${jrt_fs_jar}" DESTINATION "${jrt_fixture_dir}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${jrt_fs_jar}")
    target_compile_definitions(hippocafe_tests PRIVATE HIPPOCAFE_JRT_FIXTURE_DIR="${jrt_fixture_dir}")
  endif()

  if(TARGET hippocafe_roundtrip)
    add_test(NAME hippocafe.jvm_roundtrip COMMAND "${CMAKE_COMMAND}"
      "-DROUNDTRIP=$<TARGET_FILE:hippocafe_roundtrip>"
      "-DJAVA=${Java_JAVA_EXECUTABLE}"
      "-DFIXTURE_DIR=${fixture_dir}"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/jvm-roundtrip"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/run_java.cmake"
    )
  endif()
endif()
