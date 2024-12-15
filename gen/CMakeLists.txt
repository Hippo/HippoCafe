find_package(libzip REQUIRED)

add_executable(libcafe_gen gen.cpp)

target_link_libraries(libcafe_gen PRIVATE libcafe::cafe libzip::zip)