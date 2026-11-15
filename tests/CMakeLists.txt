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
  cmake_path(GET java_bin_dir PARENT_PATH java_home)
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

  find_program(hippocafe_jimage jimage HINTS "${java_bin_dir}" NO_DEFAULT_PATH)
  if(EXISTS "${java_home}/jmods/java.base.jmod" OR
     EXISTS "${java_home}/jre/lib/rt.jar" OR EXISTS "${java_home}/lib/rt.jar" OR
     (hippocafe_jimage AND EXISTS "${java_home}/lib/modules"))
    set(analysis_fixtures "${CMAKE_CURRENT_BINARY_DIR}/analysis-fixtures")
    set(analysis_sources "${fixture_dir}/java/AnalysisFixture.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/VerifyAnalysis.java")
    set(generated_verifier "${CMAKE_CURRENT_SOURCE_DIR}/integration/VerifyGenerated.java")
    add_custom_command(OUTPUT "${analysis_fixtures}/AnalysisFixture.class"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${analysis_fixtures}"
      COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -g -d "${analysis_fixtures}" ${analysis_sources}
      COMMAND "${Java_JAVAC_EXECUTABLE}" --release 8 -Xlint:-options -d "${analysis_fixtures}" "${generated_verifier}"
      BYPRODUCTS
        "${analysis_fixtures}/AnalysisFixture$Marked.class"
        "${analysis_fixtures}/AnalysisFixture$Score.class"
        "${analysis_fixtures}/AnalysisFixture$Left.class"
        "${analysis_fixtures}/AnalysisFixture$Right.class"
        "${analysis_fixtures}/AnalysisFixture$First.class"
        "${analysis_fixtures}/AnalysisFixture$Second.class"
        "${analysis_fixtures}/AnalysisFixture$BothLeft.class"
        "${analysis_fixtures}/AnalysisFixture$BothRight.class"
        "${analysis_fixtures}/VerifyAnalysis.class"
        "${analysis_fixtures}/VerifyGenerated.class"
      DEPENDS ${analysis_sources} "${generated_verifier}"
      VERBATIM
    )
    add_custom_target(hippocafe_analysis_fixtures DEPENDS "${analysis_fixtures}/AnalysisFixture.class")
    add_executable(hippocafe_rewrite_for_jvm integration/tools/rewrite_for_jvm.cpp)
    target_link_libraries(hippocafe_rewrite_for_jvm PRIVATE hippocafe::hippocafe)
    target_include_directories(hippocafe_rewrite_for_jvm PRIVATE "${PROJECT_SOURCE_DIR}/src")
    hippocafe_configure_target(hippocafe_rewrite_for_jvm)
    add_dependencies(hippocafe_rewrite_for_jvm hippocafe_analysis_fixtures)
    add_test(NAME hippocafe.jvm_analysis COMMAND "${CMAKE_COMMAND}"
      "-DREWRITE=$<TARGET_FILE:hippocafe_rewrite_for_jvm>"
      "-DJAVA=${Java_JAVA_EXECUTABLE}"
      "-DJAVA_HOME=${java_home}"
      "-DJIMAGE=${hippocafe_jimage}"
      "-DFIXTURE_DIR=${analysis_fixtures}"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/jvm-analysis"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/run_java_analysis.cmake"
    )
  endif()
endif()
