cmake_minimum_required(VERSION 3.10)
project(libcafe)

set(CMAKE_CXX_STANDARD 17)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set(CAFE_STANDALONE ON)
else ()
    set(CAFE_STANDALONE OFF)
endif ()

option(BUILD_SHARED_LIBS "Build as a shared library" OFF)
option(CAFE_BUILD_TESTS "Build tests" ${CAFE_STANDALONE})
option(CAFE_BUILD_GEN "Build the class tree generator" ${CAFE_STANDALONE})

if (${CAFE_BUILD_TESTS})
    add_subdirectory(tests)
endif ()

if (${CAFE_BUILD_GEN})
    add_subdirectory(gen)
endif ()

set(CAFE_HEADERS
        include/cafe/class_reader.hpp
        include/cafe/apidef.hpp
        include/cafe/constant_pool.hpp
        include/cafe/value.hpp
        include/cafe/constants.hpp
        include/cafe/label.hpp
        include/cafe/annotation.hpp
        include/cafe/instruction.hpp
        include/cafe/class_file.hpp
        include/cafe/class_writer.hpp
        include/cafe/data_writer.hpp
        src/visitor.hpp
        include/cafe/cafe.hpp
        include/cafe/class_tree.hpp
        include/cafe/data_reader.hpp
        include/cafe/analysis.hpp
)
set(CAFE_SOURCES
        src/class_reader.cpp
        src/value.cpp
        src/constants.cpp
        src/label.cpp
        src/annotation.cpp
        src/instruction.cpp
        src/class_file.cpp
        src/class_writer.cpp
        src/data_writer.cpp
        src/visitor.cpp
        src/class_tree.cpp
        src/data_reader.cpp
        src/analysis.cpp
)
set(CAFE_GEN_SOURCES
        src/gen/gen_class_tree.cpp src/gen/gen_class_tree.hpp
        src/gen/gen_class_tree_0.cpp
        src/gen/gen_class_tree_1.cpp
        src/gen/gen_class_tree_2.cpp
        src/gen/gen_class_tree_3.cpp
        src/gen/gen_class_tree_4.cpp
        src/gen/gen_class_tree_5.cpp
        src/gen/gen_class_tree_6.cpp
        src/gen/gen_class_tree_7.cpp
        src/gen/gen_class_tree_8.cpp
        src/gen/gen_class_tree_9.cpp
        src/gen/gen_class_tree_10.cpp
        src/gen/gen_class_tree_11.cpp
        src/gen/gen_class_tree_12.cpp
        src/gen/gen_class_tree_13.cpp
        src/gen/gen_class_tree_14.cpp
        src/gen/gen_class_tree_15.cpp
        src/gen/gen_class_tree_16.cpp
        src/gen/gen_class_tree_17.cpp
        src/gen/gen_class_tree_18.cpp
        src/gen/gen_class_tree_19.cpp
        src/gen/gen_class_tree_20.cpp
        src/gen/gen_class_tree_21.cpp
        src/gen/gen_class_tree_22.cpp
        src/gen/gen_class_tree_23.cpp
        src/gen/gen_class_tree_24.cpp
        src/gen/gen_class_tree_25.cpp
        src/gen/gen_class_tree_26.cpp
        src/gen/gen_class_tree_27.cpp
        src/gen/gen_class_tree_28.cpp
        src/gen/gen_class_tree_29.cpp
        src/gen/gen_class_tree_30.cpp
        src/gen/gen_class_tree_31.cpp
        src/gen/gen_class_tree_32.cpp
)

add_library(cafe
        ${CAFE_HEADERS}
        ${CAFE_SOURCES}
        ${CAFE_GEN_SOURCES}
)

add_library(libcafe::cafe ALIAS cafe)

if (${BUILD_SHARED_LIBS} AND ${WIN32})
    target_compile_definitions(cafe PUBLIC CAFE_SHARED_LIB)
    target_compile_definitions(cafe PRIVATE CAFE_EXPORTS)
endif ()

if (${BUILD_SHARED_LIBS})
    set_property(TARGET cafe PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()

target_include_directories(cafe PUBLIC ${PROJECT_SOURCE_DIR}/include)