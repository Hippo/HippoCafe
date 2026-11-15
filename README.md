# hippocafe

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

## Example usage

Read and serialize an owned class-file buffer. Compatible reads report recoveries;
write diagnostics remain in the returned result.

<!-- snippet: examples/quickstart.cpp:read-write -->
```cpp
#include <cstddef>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hippo/cafe.hpp>

std::expected<cafe::WriteResult, std::string> roundtrip(std::vector<std::byte> bytes) {
  auto parsed = cafe::read_class(std::move(bytes));
  if (!parsed)
    return std::unexpected(std::move(parsed.error().detail));
  for (const auto& diagnostic : parsed->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';

  auto written = cafe::write_class(parsed->class_file);
  if (!written)
    return std::unexpected(std::move(written.error().detail));
  return std::move(*written);
}
```
<!-- end snippet -->

Save the returned `bytes` and inspect its `diagnostics`. The span reader overload
copies input; the moved-vector overload retains it. See the complete
`examples/roundtrip.cpp` executable for checked file I/O.

## Compute stack map frames

After editing a class, pass `CodeAnalysisMode::ComputeFrames` and an
`AnalysisContext` to `write_class`. The writer recomputes max stack, max locals,
and verifier frames for the output.

<!-- snippet: examples/edit_and_compute.cpp:compute-frames -->
```cpp
std::expected<cafe::WriteResult, std::string> write_with_frames(
    const cafe::ClassFile& file, const std::filesystem::path& java_home, std::uint16_t target_release) {
  cafe::classpath::ClassPathBuilder builder;
  builder.set_target_release(target_release);
  if (auto added = builder.add_java_home(java_home); !added)
    return std::unexpected(std::move(added.error().detail));
  cafe::analysis::AnalysisContext context{
      cafe::classpath::ClassHierarchy{cafe::classpath::ClassResolver{builder.build()}},
  };
  auto written = cafe::write_class(
      file, {.code_analysis = cafe::CodeAnalysisMode::ComputeFrames}, context);
  if (!written)
    return std::unexpected(written.error().path + ": " + written.error().detail);
  return std::move(*written);
}
```
<!-- end snippet -->

For Java 17, call `write_with_frames(file, "/path/to/jdk-17", 17)` with a JDK that
includes `jmods/`. Register any application dependencies with `add_directory`,
`add_jar`, or `add_jmod` before calling `build()`. Inspect the result's
diagnostics and save its bytes just as you would for a round trip.
See the runnable example in the [usage guide](docs/usage.md).

## Documentation

- [Guide and API reference](docs/README.md)
- [Format support and guarantees](docs/compatibility.md)
- [Architecture](docs/architecture.md)
- [Development and validation](docs/development.md)

Generate the full API and implementation reference with `doxygen Doxyfile`.
The HTML entry point is `build/docs/html/index.html`.

[License](LICENSE)
