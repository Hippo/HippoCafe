cmake_minimum_required(VERSION 3.28)
project(HippoCafeConsumer LANGUAGES CXX)

find_package(HippoCafe CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE hippocafe::hippocafe)

enable_testing()
add_test(NAME installed.consumer COMMAND consumer)
