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
        include/cafe/class_file/constant_pool.hpp
        include/cafe/class_file/attribute.hpp
        include/cafe/class_file/stackmap.hpp
        include/cafe/model/annotation.hpp
        include/cafe/constants.hpp
        src/class_file/visitor.hpp
        src/class_file/visitor.cpp
        include/cafe/value.hpp
        src/value.cpp
        include/cafe/model/instruction.hpp
        src/model/annotation.cpp
        src/class_file/constast_pool.cpp
        src/model/instruction.cpp
        src/model/composer.hpp
        src/model/composer.cpp
        src/class_file/parser.hpp
        src/class_file/parser.cpp
        src/constants.cpp
        include/cafe/model/label.hpp
        src/model/label.cpp
        include/cafe/class_rw.hpp
        src/class_rw.cpp
        src/model/decomposer.hpp
        src/model/decomposer.cpp
        include/cafe/model/analysis.hpp
        src/model/analysis.cpp
        include/cafe/class_file/class_file.hpp
        include/cafe/class_file/annotation.hpp
        include/cafe/model/class_model.hpp
        src/class_file/class_file.cpp
        include/cafe/class_file.hpp
        src/model/class_model.cpp
        include/cafe/model.hpp
        include/hippo/cafe.hpp
)

target_include_directories(HippoCafe PUBLIC ${PROJECT_SOURCE_DIR}/include)
