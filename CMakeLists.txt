cmake_minimum_required(VERSION 3.10)
project(HippoCafe)

set(CMAKE_CXX_STANDARD 17)


if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set(CAFE_STANDALONE ON)
else ()
    set(CAFE_STANDALONE OFF)
endif ()


option(BUILD_SHARED_LIBS "Build as a shared library" OFF)
option(CAFE_BUILD_TESTS "Build tests" ${CAFE_STANDALONE})


if (${CAFE_BUILD_TESTS})
    add_subdirectory(tests)
endif ()


add_library(HippoCafe include/cafe/data_rw.hpp
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
        include/cafe/apidef.hpp)

if (${BUILD_SHARED_LIBS})
    target_compile_definitions(HippoCafe PRIVATE CAFE_BUILDING_LIB)
else ()
    target_compile_definitions(HippoCafe PUBLIC CAFE_STATIC_LIB)
endif ()


if (${BUILD_SHARED_LIBS})
    set_property(TARGET HippoCafe PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()

target_compile_options(HippoCafe PRIVATE /W0)

target_include_directories(HippoCafe PUBLIC ${PROJECT_SOURCE_DIR}/include)
