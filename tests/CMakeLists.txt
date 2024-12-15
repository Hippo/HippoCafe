include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(libcafe_test
        class_reader_test.cpp
        class_writer_test.cpp
        desc_parse_test.cpp
        class_tree_test.cpp
        analysis_test.cpp
)

enable_testing()
add_test(NAME libcafe_test COMMAND libcafe_test)
target_link_libraries(libcafe_test PRIVATE gtest_main libcafe::cafe)


add_custom_command(TARGET libcafe_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${CMAKE_CURRENT_BINARY_DIR}/data)