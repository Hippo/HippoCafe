cmake_minimum_required(VERSION 3.28)
project(HippoCafe VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()
include(GNUInstallDirs)

option(HIPPOCAFE_BUILD_EXAMPLES "Build the class-file examples" ${PROJECT_IS_TOP_LEVEL})
option(HIPPOCAFE_BUILD_BENCHMARKS "Build the class-file benchmarks" OFF)
option(HIPPOCAFE_BUILD_FUZZERS "Build coverage-guided fuzz targets with Clang" OFF)
option(HIPPOCAFE_ENABLE_SANITIZERS "Instrument HippoCafe with ASan and UBSan" OFF)
option(HIPPOCAFE_WARNINGS_AS_ERRORS "Treat HippoCafe compiler warnings as errors" OFF)
option(HIPPOCAFE_ENABLE_CLANG_TIDY "Run clang-tidy on HippoCafe sources" OFF)

if(HIPPOCAFE_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "HIPPOCAFE_BUILD_FUZZERS requires Clang with compiler-rt")
endif()
if(HIPPOCAFE_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "HIPPOCAFE_ENABLE_SANITIZERS requires GCC or Clang")
endif()
if(HIPPOCAFE_ENABLE_CLANG_TIDY)
  find_program(hippocafe_clang_tidy NAMES clang-tidy REQUIRED)
endif()

function(hippocafe_configure_target target)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF CXX_SCAN_FOR_MODULES OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(HIPPOCAFE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(HIPPOCAFE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(HIPPOCAFE_ENABLE_SANITIZERS OR HIPPOCAFE_BUILD_FUZZERS)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
  if(HIPPOCAFE_ENABLE_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${hippocafe_clang_tidy};--checks=clang-analyzer-*")
  endif()
endfunction()

if(BUILD_TESTING OR HIPPOCAFE_BUILD_FUZZERS)
  include(FetchContent)
  set(INSTALL_GTEST OFF)
  FetchContent_Declare(googletest
    URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    EXCLUDE_FROM_ALL SYSTEM
  )
  if(HIPPOCAFE_BUILD_FUZZERS)
    include(cmake/FuzzDependencies.cmake)
  else()
    FetchContent_MakeAvailable(googletest)
  endif()
endif()

add_library(hippocafe STATIC
  src/bytecode/code.cpp
  src/bytecode/instruction_builder.cpp
  src/bytecode/opcode_info.cpp
  src/io/attribute_reader.cpp
  src/io/attribute_writer.cpp
  src/io/class_format.cpp
  src/io/class_reader.cpp
  src/io/class_writer.cpp
  src/io/code_decoder.cpp
  src/io/code_encoder.cpp
  src/io/code_layout.cpp
  src/io/code_operands.cpp
  src/io/code_writer.cpp
  src/io/constant_pool_reader.cpp
  src/io/opaque_attribute.cpp
  src/model/attribute.cpp
  src/model/class_file.cpp
  src/model/constant_pool.cpp
)
add_library(hippocafe::hippocafe ALIAS hippocafe)
target_compile_features(hippocafe PUBLIC cxx_std_23)
set_target_properties(hippocafe PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(hippocafe
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE src
)
hippocafe_configure_target(hippocafe)
if(HIPPOCAFE_BUILD_FUZZERS)
  # Both fuzz engines use the inline counters. libFuzzer also needs the
  # corresponding PC table for the library's instrumentation.
  target_compile_options(hippocafe PRIVATE -fsanitize-coverage=pc-table)
endif()

if(HIPPOCAFE_BUILD_EXAMPLES)
  foreach(example IN ITEMS inspect_class roundtrip)
    add_executable(hippocafe_${example} examples/${example}.cpp)
    target_link_libraries(hippocafe_${example} PRIVATE hippocafe::hippocafe)
    hippocafe_configure_target(hippocafe_${example})
  endforeach()
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
if(HIPPOCAFE_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
if(HIPPOCAFE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(NOT HIPPOCAFE_ENABLE_SANITIZERS AND NOT HIPPOCAFE_BUILD_FUZZERS)
  include(CMakePackageConfigHelpers)
  set(hippocafe_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/HippoCafe")
  configure_package_config_file(cmake/HippoCafeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/HippoCafeConfig.cmake"
    INSTALL_DESTINATION "${hippocafe_package_dir}"
  )
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/HippoCafeConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
  )
  install(TARGETS hippocafe EXPORT HippoCafeTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
  install(EXPORT HippoCafeTargets NAMESPACE hippocafe:: DESTINATION "${hippocafe_package_dir}")
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HippoCafeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HippoCafeConfigVersion.cmake"
    DESTINATION "${hippocafe_package_dir}"
  )
endif()
