cmake_minimum_required(VERSION 3.10)
project(HippoCafe)

set(CMAKE_CXX_STANDARD 17)

option(BUILD_SHARED_LIBS "Build as a shared library" OFF)
option(BUILD_TESTS "Build tests" ON)

if (BUILD_TESTS)
    add_subdirectory(tests)
endif ()

add_library(HippoCafe
        include/cafe/data_rw.hpp
        src/data_rw.cpp
        include/cafe/constant_pool.hpp
        include/cafe/class_rw.hpp
        src/class_rw.cpp
        include/cafe/attribute.hpp
        include/cafe/stackmap.hpp
        include/cafe/annotation.hpp
        include/cafe/class_file.hpp
        include/cafe/constants.hpp
)

target_include_directories(HippoCafe PUBLIC ${PROJECT_SOURCE_DIR}/include)
