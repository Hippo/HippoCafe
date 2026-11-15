# 01 — Project Structure, Naming, and Module Boundaries

## Intent

Build a reusable C++23 class-file library, not an application or a bytecode deobfuscation framework. The library should be easy to link as one CMake target while its internals remain strongly separated. Header boundaries must reflect stable public concepts; parser implementation details and allocator machinery remain private.

## Naming (normative)

Follow Rust naming conventions in C++:

| Entity | Convention | Example |
|---|---|---|
| Class, struct, enum, alias, concept | `PascalCase` | `ClassFile`, `MethodCode`, `InstructionId`, `AttributeKind` |
| Enum variant | `PascalCase` | `Opcode::IAdd`, `ParseMode::Compatible` |
| Function, method, variable, data member | `snake_case` | `read_class`, `create_label`, `max_locals`, `source_bytes` |
| Compile-time constant | `SCREAMING_SNAKE_CASE` | `CLASS_MAGIC`, `MAX_CODE_SIZE` |
| Namespace, directory, `.hpp` / `.cpp` filename | `snake_case` | `cafe::io`, `constant_pool.hpp` |
| Template parameter representing a type | `PascalCase` | `Value`, `Allocator` |
| C macro (only if unavoidable) | `SCREAMING_SNAKE_CASE` and prefix | `CAFE_EXPORT` |

Use `cafe` as the top-level namespace. Use `cafe::detail` only internally; no internal types in public method signatures. Do not reserve names beginning with `_` followed by uppercase letters, and do not expose raw wire-format names as idiomatic C++ members if a clearer name exists. Keep JVM strings and opcode mnemonics in the specification's spelling where serialized; the C++ API spelling is not the wire spelling.

Use no Hungarian notation or `m_`-prefixed fields. No unnecessary `I` interface prefixes. Public functions return named result/error types; use `std::expected` rather than boolean success + output parameters. Follow project `.clang-format`; clang-tidy and sanitizers must work with tests.

## Proposed tree

```text
HippoCafe/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
│   └── HippoCafeConfig.cmake.in
├── include/
│   ├── hippo/cafe.hpp             # Convenience umbrella
│   └── cafe/
│       ├── class_file.hpp
│       ├── class_version.hpp
│       ├── constant_pool.hpp
│       ├── field.hpp
│       ├── method.hpp
│       ├── attribute.hpp
│       ├── annotation.hpp
│       ├── ids.hpp
│       ├── error.hpp
│       ├── read_options.hpp
│       ├── write_options.hpp
│       ├── bytecode/
│       │   ├── opcode.hpp
│       │   ├── instruction.hpp
│       │   ├── code.hpp
│       │   ├── label.hpp
│       │   ├── switch.hpp
│       │   └── builder.hpp
│       └── io/
│           ├── class_reader.hpp
│           └── class_writer.hpp
├── src/
│   ├── model/
│   │   ├── class_file.cpp
│   │   ├── constant_pool.cpp
│   │   ├── attribute.cpp
│   │   └── class_storage.hpp
│   ├── bytecode/
│   │   ├── code.cpp
│   │   ├── opcode_info.cpp
│   │   ├── switch_storage.cpp
│   │   └── instruction_builder.cpp
│   ├── io/
│   │   ├── binary_reader.hpp
│   │   ├── binary_writer.hpp
│   │   ├── class_reader.cpp
│   │   ├── class_writer.cpp
│   │   ├── constant_pool_reader.cpp
│   │   ├── attribute_reader.cpp
│   │   ├── attribute_writer.cpp
│   │   ├── code_decoder.cpp
│   │   ├── code_layout.cpp
│   │   └── code_encoder.cpp
│   └── detail/
│       ├── slot_map.hpp
│       └── checked_arithmetic.hpp
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── regression/
│   └── fixtures/
├── fuzz/
│   ├── class_reader_fuzz.cpp
│   └── roundtrip_fuzz.cpp
├── benchmarks/
│   ├── parse.cpp
│   ├── write.cpp
│   └── instructions.cpp
├── examples/
│   ├── inspect_class.cpp
│   └── roundtrip.cpp
└── docs/
    └── ...
```

This is a conceptual organization rather than a requirement to make empty files. Only create source/header files when they contain real responsibilities. The `analysis/` and `transform/` directories are **not** part of pass one; add them when implemented. Do not build speculative interfaces for CFG, SSA, frame calculation, or automatic max computation.

## Dependency direction

```text
public read_class / write_class
          |
          v
    io implementation ------> Model (ClassFile/ConstantPool/Attribute)
          |                       |
          v                       v
  bounded binary helpers      MethodCode / Instruction
                                   |
                                   v
                              private slot storage
```

- `Model` and `Bytecode` do not depend on the reader, writer, a Java runtime, a classpath, or analysis.
- `io` depends on model and bytecode. It owns wire-format rules and error offsets.
- `ClassFile` owns the resources. Neither `Instruction` nor `MethodCode` owns a global allocator.
- Parser errors and writer errors are separate; shared `Diagnostic` is neutral.
- Public headers may depend on standard C++ library components but must not expose internal slot storage.
- Files in `src/detail/` and internal headers are never installed as public API.

## Public API direction (illustrative, implement actual compilable signatures)

```cpp
namespace cafe {

struct ReadOptions;
struct WriteOptions;
struct ReadError;
struct WriteError;
struct Diagnostic;

struct ReadResult {
    ClassFile class_file;
    std::vector<Diagnostic> diagnostics;
};

struct WriteResult {
    std::vector<std::byte> bytes;
    std::vector<Diagnostic> diagnostics;
};

std::expected<ReadResult, ReadError> read_class(
    std::span<const std::byte> data,
    const ReadOptions& options = {}
);

std::expected<WriteResult, WriteError> write_class(
    const ClassFile& class_file,
    const WriteOptions& options = {}
);

} // namespace cafe
```

The result shape can change if a compelling implementation detail requires it, but diagnostics cannot be silently discarded. An overload accepting `std::vector<std::byte>&&` should transfer the backing storage without copying. If an input stream overload is offered, read it under the same size limits and then parse through the common span-backed implementation.

## Build / packaging

- One primary target: `hippocafe`, with alias `hippocafe::hippocafe`.
- Require C++23 via `target_compile_features(hippocafe PUBLIC cxx_std_23)`.
- Never leak sanitizer/benchmark/test-only flags into installed exports.
- Provide `BUILD_TESTING` integration via CTest; optional fuzz and benchmark build switches.
- Public installable headers plus namespaced exported CMake package config.
- Avoid unnecessary external runtime dependencies for this first pass. Use standard containers, PMR and expected.
- Build and test on at least two standard C++ toolchains where available. Treat compiler portability warnings as failures in CI.

## Code quality and errors

- No `assert` for untrusted input validation; assertions are for programmer invariants.
- No UB on malformed input; no signed-overflow index arithmetic; no unchecked `reinterpret_cast` of on-disk structs.
- No exception-based parsing protocol. Resource exhaustion (`std::bad_alloc`) may remain an exceptional failure, but read limits must proactively prevent pathological allocations and translate them into structured errors when possible.
- Separate `ReadError` (`offset`, `path`, `kind`) from recoverable `Diagnostic` (`severity`, `offset`, `attribute_path`, `reason`, `action`).
- Avoid eager string decoding, intermediate copied constant pools and pointer-rich instruction graphs on the hot path.
- Tests are part of the feature. Don't merge public APIs lacking misuse/edge-case tests.

## Typical usage

```cpp
const auto parsed = cafe::read_class(input_bytes);
if (!parsed) {
    report(parsed.error());
    return;
}
for (const auto& diagnostic : parsed->diagnostics) {
    report(diagnostic);
}
const auto written = cafe::write_class(parsed->class_file);
if (!written) {
    report(written.error());
    return;
}
consume(written->bytes);
```

Naming in the public headers is a contractual choice. Internal algorithms may be replaced following benchmarks; visible types should stay deliberate, minimal and version-aware.
