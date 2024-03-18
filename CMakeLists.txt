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


add_library(HippoCafe
        include/cafe/class_reader.hpp
        src/class_reader.cpp
        include/cafe/apidef.hpp
        include/cafe/constant_pool.hpp
        include/cafe/value.hpp
        src/vaule.cpp
        include/cafe/constants.hpp
        src/constants.cpp
        include/cafe/label.hpp
        src/label.cpp
        include/cafe/annotation.hpp
        src/annotation.cpp
        include/cafe/instruction.hpp
        src/instruction.cpp
        include/cafe/class_file.hpp
        src/class_file.cpp
        include/cafe/stub.hpp
        src/stub.cpp
        include/cafe/class_writer.hpp
        src/class_writer.cpp
        src/bytebuf.cpp
        src/bytebuf.hpp
        src/visitor.hpp
        src/visitor.cpp
        include/hippo/cafe.hpp)

if (${BUILD_SHARED_LIBS})
    target_compile_definitions(HippoCafe PRIVATE CAFE_BUILDING_LIB)
else ()
    target_compile_definitions(HippoCafe PUBLIC CAFE_STATIC_LIB)
endif ()

if (${BUILD_SHARED_LIBS})
    set_property(TARGET HippoCafe PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()

target_include_directories(HippoCafe PUBLIC ${PROJECT_SOURCE_DIR}/include)