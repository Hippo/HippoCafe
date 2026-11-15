# HippoCafe

HippoCafe is a C++23 library for inspecting, editing, and writing JVM class files. It keeps constant-pool indices and attribute order stable. Unchanged methods retain their original Code bytes, including verifier metadata, debug information, and unusual encodings.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The library target is `hippocafe::hippocafe`. Installed consumers use `find_package(HippoCafe CONFIG REQUIRED)` and link the same target. A library-only build can set `BUILD_TESTING=OFF` and `HIPPOCAFE_BUILD_EXAMPLES=OFF`.

The test and fuzz builds fetch pinned GoogleTest and FuzzTest dependencies with CMake FetchContent. The `sanitized` preset uses Clang with ASan and UBSan. The `release-benchmarks` preset builds the benchmark executable. See the [fixture manifest](tests/fixtures/README.md) for the supplied and generated test inputs.

## Read and write

```cpp
#include <hippo/cafe.hpp>

// bytes is a std::vector<std::byte> loaded from a class file.
auto parsed = cafe::read_class(std::move(bytes));
if (!parsed) {
  report(parsed.error());
  return;
}
for (const auto& diagnostic : parsed->diagnostics)
  report(diagnostic);

auto output = cafe::write_class(parsed->class_file);
if (!output) {
  report(output.error());
  return;
}
save(output->bytes);
```

The span overload copies the input once. The moved-vector overload keeps the original buffer. The resulting class owns the buffer and the storage referenced by its model.

The default compatible reader drops bounded malformed or version-inapplicable optional attributes and reports each recovery, subject to the diagnostic limit. It drops trailing bytes on reserialization. Strict mode reports these conditions as errors. Both modes reject attribute overruns, unknown constant tags, and unsafe essential references. Compatible reads can opt into opaque Code for framed methods with undecodable bytecode. Verbatim emission of opaque Code requires a separate writer opt-in.

The library understands all standard constant tags and opcode forms, including the historical short Code header through version 45.2 and legacy subroutines through major version 50. It exposes typed Code, ConstantValue, Exceptions, SourceFile, Signature, BootstrapMethods, Synthetic, and Deprecated. Annotations, frames, debug tables, records, module and nest metadata, and vendor attributes remain opaque. The reader checks the structure of supported opaque bodies before retaining their bytes.

## Edit a method

`find_method("run", "()V")` and `find_field("count", "I")` match an exact JVM name and descriptor. They return an optional table index for the edit and erase APIs.

`ClassFile::edit_method(index)` returns a borrowed `MethodEditor` with access to the method through `method()` and its mutable `CodeAttribute` through `code()`. `ClassFile::edit_code(index)` is a shortcut to the same Code attribute. Acquire a fresh method editor after adding or erasing a method.

Scan the decoded `MethodCode` with `for (const auto [id, instruction] : code.instructions())` and save the handle where you want to edit. Acquire a fresh instruction range after an edit.

```cpp
// The anchor has an initialized int in slot 300.
const std::array<cafe::InstructionBuilder, 2> patch{
    cafe::Instruction::iload(300),
    cafe::Instruction::pop(),
};
auto inserted = code.insert_before(anchor, patch);
if (!inserted)
  report(inserted.error());
```

`append`, `insert_before`, and `insert_at` each accept a block and apply it atomically in input order. A failure reports its cause and, when applicable, the zero-based position in the patch. `insert_before` keeps labels on the original anchor. A successful, nonempty `insert_at` rebinds the selected label to the first inserted instruction. The writer selects `wide` automatically for the local load.

`write_class` preserves the original encodings by default. `force_reencode` exercises the instruction encoder. A change to code or its layout requires fresh metadata from `CodeAttribute::supply_metadata` and the `CallerSuppliedMetadata` write policy. The supplied metadata covers the current instruction revision and the chosen output encoding. The caller supplies stack and local limits, handlers, and any required frames and offset-bearing attributes.

Use `erase_method(index)` and `erase_field(index)` to remove declarations. Later indices shift down, and the constant pool retains its slots. Update any code that refers to a removed declaration as part of the edit.

## Fuzz

```sh
cmake --preset fuzz
cmake --build --preset fuzz
build/fuzz/fuzz/hippocafe_fuzz --fuzz_for=10s
mkdir -p build/fuzz/fuzz/mutated
build/fuzz/fuzz/hippocafe_libfuzzer -seed=314159 -runs=5000 build/fuzz/fuzz/mutated build/fuzz/fuzz/seeds
```

The round-trip example is available as `hippocafe_roundtrip input.class output.class`. The structural inspection example is `hippocafe_inspect_class input.class`.
