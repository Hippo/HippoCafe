include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(HippoCafeTest data_rw_test.cpp
        class_rw_test.cpp)

enable_testing()
add_test(NAME HippoCafeTest COMMAND HippoCafeTest)
target_link_libraries(HippoCafeTest PUBLIC gtest_main HippoCafe)