# Development

## Toolchain and dependencies

The library requires C++23 and CMake 3.28 or newer. Presets use Ninja. CI builds with
GCC and Clang; the standard library must provide C++23 `std::expected` and PMR support.
A JDK 17 or newer enables generated Java fixtures and JVM integration checks.
Python 3 runs the documentation consistency check and fixture-generation tools.
Doxygen is optional unless building the reference; Graphviz and LaTeX are unnecessary.

CMake FetchContent retrieves checksum-pinned minizip-ng and zlib-ng sources. Tests use
pinned GoogleTest; fuzz builds additionally use pinned FuzzTest dependencies. Keep
license installation and exported dependencies synchronized when changing packaging.
Normal builds fetch dependencies if they are not already available. Standard CMake
FetchContent source overrides can supply prepopulated offline dependencies.

## Build and consume

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For a library-only release installation:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHIPPOCAFE_BUILD_EXAMPLES=OFF
cmake --build build/release --config Release
cmake --install build/release --config Release --prefix /your/install/prefix
```

Consumers set `CMAKE_PREFIX_PATH` as needed, call `find_package(HippoCafe CONFIG REQUIRED)`,
and link `hippocafe::hippocafe`. The same target is available with `add_subdirectory`.
The package is static and carries its required archive/compression dependencies.
Instrumented sanitizer/fuzz builds intentionally omit install/export rules.

| Option | Purpose | Default |
|---|---|---|
| `BUILD_TESTING` | Unit, corpus, checksum, header, and available JVM checks | On for top-level CTest builds |
| `HIPPOCAFE_BUILD_EXAMPLES` | Inspection, round-trip, bytecode editing, and SSA examples | On at top level |
| `HIPPOCAFE_BUILD_BENCHMARKS` | Release measurement executable | Off |
| `HIPPOCAFE_BUILD_FUZZERS` | FuzzTest and libFuzzer targets; requires Clang C/C++ | Off |
| `HIPPOCAFE_ENABLE_SANITIZERS` | ASan and UBSan instrumentation; requires GCC/Clang | Off |
| `HIPPOCAFE_WARNINGS_AS_ERRORS` | Project compiler warnings fail builds | Off; on in debug-derived presets |
| `HIPPOCAFE_ENABLE_CLANG_TIDY` | Apply the checked-in analysis policy during compilation | Off |
| `HIPPOCAFE_BUILD_DOCS` | Enable the `hippocafe_docs` target | Off |

Examples are compiled with project warnings. The quickstart exercises both class
serialization and an in-memory archive edit. `hippocafe_edit_and_compute input.class
output.class java-home target-release` demonstrates single and block insertion,
computes frames, and saves the result. Supply a matching runtime and release;
add application dependencies to the example for non-runtime references.

## Quality checks

Follow `.clang-format`: two-space indentation and the existing LLVM-derived style.
Use PascalCase types/enum values, snake_case functions/members, and uppercase constants.
Keep public headers independently includable and implementation headers private.

Borrow inputs with views or const references when ownership stays with the caller.
Move owned buffers and error records when handing them off. Keep return-value
elision available by returning named local results directly; use `std::move` when
transferring a subobject, such as an error inside `std::expected`. A const value
cannot normally be moved, so avoid const on locals that will transfer ownership.
Retain copies needed for independent snapshots or allocator-bound storage.

```sh
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
python3 tools/check_docs.py
cmake -S . -B build/quality -DBUILD_TESTING=OFF -DHIPPOCAFE_ENABLE_CLANG_TIDY=ON -DHIPPOCAFE_BUILD_DOCS=ON
cmake --build build/quality
cmake --build build/quality --target hippocafe_docs
python3 tools/check_docs.py --html build/quality/docs/html
```

The HTML check validates file, API-symbol, and guide links. It excludes generated
alphabetical member-index shortcuts: Doxygen 1.18 misencodes the destructor heading
and omits some small enum/type index headings. Those shortcuts still open the index
page, but may not jump to a letter. Generate into an empty output directory when
checking deleted symbols, because Doxygen retains files from earlier generations.

The checked-in `.clang-tidy` selects analyzer, lifetime, optional-access, narrowing,
exception, and unnecessary-copy checks. Findings are errors. A local suppression
must name one check and explain an invariant verified by the surrounding code or
an earlier validation stage. Do not suppress arbitrary malformed-input handling.

Readiness requires passing the GCC debug and Clang sanitizer suites, the compiled
examples, independent public-header compilation, documentation checks, and an
installed-package consumer. CI also runs bounded seeded fuzz smoke tests. Changes to
behavior need regression coverage for the affected failure/rollback/limit contract;
comment-only changes do not need artificial tests.

Test builds compile each installed header as the only include in a separate
translation unit. Check the exported package after an ordinary, non-instrumented build:

```sh
cmake --install build/debug --prefix "$PWD/build/install"
cmake -S tests/package_consumer -B build/consumer -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/consumer
ctest --test-dir build/consumer --output-on-failure
```

## Fixtures and JVM checks

The [fixture manifest](../tests/fixtures/README.md) owns fixture provenance, generation
instructions, and redistribution constraints. Checked-in fixture checksums are tested;
do not casually regenerate them. Java sources compile into the build tree when the
required JDK tools are available. Integration tests check round trips, emitted verifier
metadata, JAR/JMOD behavior, and selected installed runtime classes. The edit/frame
example is also executed and its output verified by the JVM; its invalid-argument
and I/O error paths are checked. Tests can extract a runtime image into a temporary
JMOD fixture when the installed JDK supplies no runtime archives.

The SSA integration test launches the original and rewritten classes in separate
`java -Xverify:all` processes. It compares execution traces and checks the
expected results of an SSA edit and an SSA-authored method. Generated protected
loops exercise competing handlers and record side effects around each throwing
call. The `jvm_ssa_repeated` tests compile a counting loop with and without
debug information, edit its addend, and check instruction and local counts
through five successive rewrites. The debug case reads the slots advertised
for both variables while the loop runs. CI runs the GCC
debug suite on Java 17 and the Clang sanitizer suite on Java 25.

```sh
cmake --preset sanitized
cmake --build --preset sanitized
ctest --preset sanitized
```

Keep leak checking enabled in CI. Environments that restrict process tracing may
prevent LeakSanitizer from starting; `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitized`
can exercise ASan/UBSan locally, but this does not constitute a leak-check pass.

## Fuzzing and performance

```sh
cmake --preset fuzz
cmake --build --preset fuzz
build/fuzz/fuzz/hippocafe_fuzz --fuzz_for=10s
mkdir -p build/fuzz/fuzz/mutated
build/fuzz/fuzz/hippocafe_libfuzzer -seed=314159 -runs=5000 -max_len=524288 build/fuzz/fuzz/mutated build/fuzz/fuzz/seeds build/fuzz/fuzz/archive-seeds
```

The two engines exercise parsing, preservation/re-encoding, bytecode analysis,
SSA construction, edited emission and re-lifting, and archive/classpath boundaries.
The unit suite requires every method in a set of checked-in SSA fixtures to
lower and re-lift. On an expected lowering failure the raw-class fuzzer
checks the source stamp, a live instruction handle, and the re-encoded bytes.
Keep mutations and failure artifacts in the build tree. Reduce reproducible
failures before adding fixtures or regression cases.

The `release-benchmarks` preset builds `hippocafe_benchmarks`. Its arguments select a
fixture directory, iteration count, and optional Java home. Measurements cover parsing,
writing, memory, CFG/frame work, and optional runtime indexing/hierarchy resolution.
The SSA cases edit an addition on the first rewrite and use each strictly reparsed
output as the input to the next rewrite. They report total lift and lower times
and show class size, instruction count, and local slots at input, after the edit,
and after the final rewrite for an arithmetic chain and a loop with two carried values.
Compare identical fixtures and toolchain settings when changing allocation or hot paths.

## Documentation conventions

The root README is the entry point. The five guides explain current workflows,
architecture, and compatibility; implementation plans and historical completion notes
belong in version-control history, not the maintained documentation tree.

Declarations own Doxygen contracts. Document errors, parameters, result ownership,
resource lifetimes, invalidation, units, and concurrency constraints where relevant.
Use `/** ... */` for contracts and `///<` for concise enum/member meanings. Local
comments explain non-obvious algorithm or wire-format constraints. Do not narrate
obvious code or claim noexcept behavior merely because an API returns `std::expected`.

Named snippets in compiled examples are copied into Markdown between `snippet` markers.
`tools/check_docs.py` rejects stale copies and broken local file links. Doxygen checks
reference syntax and parameter coverage, includes private/static/anonymous-namespace
internals, and fails on warnings with `EXTRACT_ALL=NO`. Generated HTML belongs in the
build tree. Inspect representative API, implementation, and guide pages after structural
documentation changes; generating successfully is only one part of review.
