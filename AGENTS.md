# Working on HippoCafe

HippoCafe is a C++23 library for reading, editing, and writing JVM class files,
JARs, and JMODs. Use CMake 3.28+, Ninja for the presets, and a C++23 standard
library with `std::expected` and PMR. Python 3 runs the docs checks. A full JDK
17+ enables JVM integration tests. See [development](docs/development.md) for
dependency setup, packaging, and fuzzing.

## Quality checks

The [`quality` job](.github/workflows/ci.yml) is separate from the test matrix.
Run its checks from the repository root. CI uses LLVM 22. If your distribution
ships unversioned tools, check their `--version` and substitute the executable
names as needed. Use a separate build directory when switching compilers.

```sh
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format-22 --dry-run --Werror
python3 tools/check_docs.py

cmake -S . -B build/quality -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22 \
  -DBUILD_TESTING=OFF -DHIPPOCAFE_BUILD_EXAMPLES=ON \
  -DHIPPOCAFE_WARNINGS_AS_ERRORS=ON -DHIPPOCAFE_ENABLE_CLANG_TIDY=ON \
  -Dhippocafe_clang_tidy=clang-tidy-22 -DHIPPOCAFE_BUILD_DOCS=ON
cmake --build build/quality --parallel 2
cmake --build build/quality --target hippocafe_docs
python3 tools/check_docs.py --html build/quality/docs/html
```

- Apply `.clang-format` to the C++ files you touched, including new files that
  `git ls-files` will only see once tracked. Review the formatting diff for
  unrelated changes.
- `.clang-tidy` promotes every enabled finding to an error, including warnings
  in compiled examples. Check `std::expected` and `std::optional` before access,
  keep borrowed views within their owners' lifetimes, validate narrowing at wire
  boundaries, and return named locals directly to allow elision. When a warning
  cannot be resolved in code, name the specific check in the local suppression
  and explain the validated invariant.
- Update Doxygen contracts on the relevant declarations, including internal
  declarations. Keep parameter names, return values, errors, ownership, and
  invalidation rules in sync. Doxygen warnings fail the build. Use a fresh HTML
  output directory when removing or renaming symbols so stale pages cannot hide
  broken links.
- When changing a named snippet in `examples/`, update its marked Markdown copy
  in `README.md` or `docs/` and run `tools/check_docs.py` with Python 3. Examples
  should compile and handle fallible calls through their normal error paths.

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug

cmake --preset sanitized
cmake --build --preset sanitized --parallel 2
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset sanitized
```

- CI runs debug on GCC 15 with Java 17 and ASan/UBSan on Clang 22 with Java 25.
  The full test build compiles each installed header by itself and exercises
  unit, corpus, checksum, and available JVM tests. Use `ctest --preset debug -N`
  to verify that `hippocafe.jvm_*` tests were registered. For a focused GoogleTest
  run, use `build/debug/tests/hippocafe_tests --gtest_filter='Suite.Test'`.
- Check the [installed-package consumer](docs/development.md#quality-checks)
  for public header, CMake, or dependency changes. For parser, writer, bytecode,
  analysis, or archive changes, use the seeded fuzz commands in the workflow
  and keep mutated corpora and artifacts in `build/`.
- If dependency downloads are unavailable, reuse existing pinned source trees
  via `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`, `FETCHCONTENT_SOURCE_DIR_MINIZIP-NG`,
  and `FETCHCONTENT_SOURCE_DIR_ZLIB-NG` as applicable. Existing build trees may
  have these under `_deps/`.
- Report the checks run, any toolchain differences, and checks skipped or blocked.
  If local process tracing restrictions prevent LeakSanitizer from starting,
  `ASAN_OPTIONS=detect_leaks=0` can isolate ASan/UBSan coverage. Report leak
  checking separately and keep it enabled in CI.

## Where to make changes

- `include/cafe/` contains the installed API and `include/hippo/cafe.hpp` is the
  umbrella header. Add direct includes to keep public headers self-contained.
  Keep implementation headers in `src/`. Use PascalCase for types and enum
  values, snake_case for functions and members, and uppercase constants. Add
  new library translation units to the `hippocafe` target in `CMakeLists.txt`.
- `src/model/`, `src/bytecode/`, and `src/io/` own the class model, code edits,
  and class-file codecs. `src/analysis/` covers CFGs, JVM frames, and SSA.
  `src/archive/` handles JAR/JMOD I/O and `src/classpath/` handles resolution.
  Start with [architecture](docs/architecture.md) for module and lifetime rules
  and [compatibility](docs/compatibility.md) for supported behavior.
- Preserve bounded parsing, strict and compatible read behavior, and each writer
  policy's preservation guarantees. Validate malformed inputs with structured
  errors. Respect constant-pool index and code-offset remapping, borrowed views,
  and handle/revision invalidation. Keep failed edits transactional.
- Put behavioral regressions alongside the corresponding tests in `tests/unit/`
  or `tests/integration/`. Cover rollback, limits, or round-trip fidelity when
  those contracts change. Use JVM integration tests when changing verifier
  metadata or emitted code. Follow the [fixture manifest](tests/fixtures/README.md)
  for binary provenance and checksums. Keep generated build output and local-only
  fixtures out of Git.
