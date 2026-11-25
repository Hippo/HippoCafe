# HippoCafe

A C++23 library for inspecting, editing, and writing JVM class files, JARs, and JMODs.

## Building

Requires CMake 3.28+ and a C++23 standard library. CMake fetches pinned minizip-ng and
zlib-ng dependencies. Tests additionally use GoogleTest; a JDK 17+ enables JVM checks.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --config Release
cmake --install build/release --config Release --prefix /your/install/prefix
```

Link an installed package with:

```cmake
find_package(HippoCafe CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE hippocafe::hippocafe)
```

## Edit a method

Edit a class buffer by method name and descriptor, then write it using the target
JDK to compute stack map frames. This example adds a `nop` before the first
`return` in `main`.

```cpp
#include <hippo/cafe.hpp>

// bytes holds the input class, and java_home identifies the target JDK.
auto parsed = cafe::read_class(std::move(bytes)).value();
auto method = parsed.class_file.edit_method("main", "([Ljava/lang/String;)V").value();
auto match = method.find_first(cafe::pattern::opcode(cafe::Opcode::Return)).value().value();
auto inserted = method.insert_before(cafe::CodeNode{match.start()}, cafe::insn::nop()).value();
auto output = cafe::write_class_with_frames(parsed.class_file, java_home, 17).value();
```

Save `output.bytes`. The [usage guide](docs/usage.md) shows how to handle read,
edit, and write errors and diagnostics in the runnable example.

## Work with SSA

Lift an individual method with `cafe::analysis::lift_ssa`, or use `create_ssa` to
build a body for a concrete declaration. The graph exposes typed operands, phis,
and control-flow edges. `lower_ssa` installs verified bytecode for the ordinary
class writer. The [usage guide](docs/usage.md) walks through the compiled
`examples/edit_ssa.cpp` example.

## Documentation

- [Guide and API reference](docs/README.md)
- [Format support and guarantees](docs/compatibility.md)
- [Architecture](docs/architecture.md)
- [Development and validation](docs/development.md)

Generate the full API and implementation reference with `doxygen Doxyfile`.
The HTML entry point is `build/docs/html/index.html`.

[License](LICENSE)
