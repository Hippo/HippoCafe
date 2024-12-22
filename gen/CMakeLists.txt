find_package(libzip REQUIRED)

add_executable(hippocafe_gen gen.cpp)

target_link_libraries(hippocafe_gen PRIVATE hippocafe libzip::zip)