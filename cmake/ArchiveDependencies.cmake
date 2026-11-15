include(FetchContent)
include(GNUInstallDirs)

function(hippocafe_archive_dependencies)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  if(HIPPOCAFE_ENABLE_SANITIZERS OR HIPPOCAFE_BUILD_FUZZERS)
    set(dependency_scope EXCLUDE_FROM_ALL)
  endif()

  set(ZLIB_COMPAT OFF)
  set(ZLIB_ENABLE_TESTS OFF)
  set(ZLIBNG_ENABLE_TESTS OFF)
  set(WITH_GTEST OFF)
  set(WITH_GZFILEOP OFF)
  set(WITH_BENCHMARKS OFF)
  set(WITH_BENCHMARK_APPS OFF)
  set(WITH_FUZZERS OFF)
  FetchContent_Declare(zlib-ng
    URL https://codeload.github.com/zlib-ng/zlib-ng/tar.gz/refs/tags/2.2.5
    URL_HASH SHA256=5b3b022489f3ced82384f06db1e13ba148cbce38c7941e424d6cb414416acd18
    SYSTEM ${dependency_scope}
  )
  FetchContent_MakeAvailable(zlib-ng)

  # Satisfy minizip-ng's FindZLIB-NG with the pinned build target. The build
  # interface path keeps the find module's inferred link directory private.
  set(ZLIB-NG_LIBRARY zlib)
  set(ZLIB-NG_INCLUDE_DIRS "$<BUILD_INTERFACE:${zlib-ng_BINARY_DIR}>")
  set(MZ_ZLIB_FLAVOR "zlib-ng" CACHE STRING "Compression backend for the archive reader")
  set(MZ_ZLIB_FLAVOR "zlib-ng")
  set(MZ_LIB_SUFFIX "-ng" CACHE STRING "Library name suffix for the archive reader")
  set(MZ_LIB_SUFFIX "-ng")
  set(MZ_ZLIB ON)
  foreach(option IN ITEMS MZ_COMPAT MZ_BZIP2 MZ_LZMA MZ_PPMD MZ_ZSTD MZ_LIBCOMP
      MZ_FETCH_LIBS MZ_FORCE_FETCH_LIBS MZ_PKCRYPT MZ_WZAES MZ_OPENSSL MZ_LIBBSD MZ_ICONV
      MZ_COMPRESS_ONLY MZ_BUILD_TESTS MZ_BUILD_UNIT_TESTS MZ_BUILD_FUZZ_TESTS)
    set(${option} OFF)
  endforeach()
  set(MZ_DECOMPRESS_ONLY ON)
  FetchContent_Declare(minizip-ng
    URL https://codeload.github.com/zlib-ng/minizip-ng/tar.gz/refs/tags/4.1.2
    URL_HASH SHA256=3738c742c663fda43f1e510b8eeef312917581a712c89cb253f682aaef8c732f
    SYSTEM ${dependency_scope}
  )
  FetchContent_MakeAvailable(minizip-ng)
  if(HIPPOCAFE_BUILD_FUZZERS)
    # FuzzTest's inherited inline counters also cover the C archive libraries.
    # libFuzzer requires matching PC metadata for both libraries.
    target_compile_options(zlib PRIVATE -fsanitize-coverage=pc-table)
    target_compile_options(minizip-ng PRIVATE -fsanitize-coverage=pc-table)
  endif()

  if(NOT dependency_scope)
    install(FILES "${zlib-ng_SOURCE_DIR}/LICENSE.md"
      DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME zlib-ng-LICENSE)
    install(FILES "${minizip-ng_SOURCE_DIR}/LICENSE"
      DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME minizip-ng-LICENSE)
  endif()
endfunction()
