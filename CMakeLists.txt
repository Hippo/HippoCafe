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
        include/cafe/attribute.hpp
        include/cafe/stackmap.hpp
        include/cafe/annotation.hpp
        include/cafe/class_file.hpp
        include/cafe/constants.hpp
        src/class_file.cpp
        src/visitor.hpp
        src/visitor.cpp
        include/cafe/value.hpp
        src/value.cpp
        include/cafe/instruction.hpp
        src/annotation.cpp
        src/constast_pool.cpp
        src/instruction.cpp
        src/composer.hpp
        src/composer.cpp
        src/parser.hpp
        src/parser.cpp
        src/constants.cpp
        include/cafe/label.hpp
        src/label.cpp
        include/cafe/class_rw.hpp
        src/class_rw.cpp
)

target_include_directories(HippoCafe PUBLIC ${PROJECT_SOURCE_DIR}/include)
