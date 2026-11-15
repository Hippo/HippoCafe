# HippoCafe

HippoCafe is a C++23 library for inspecting, editing, and writing JVM class files, JARs, and JMODs. It keeps class-file constant-pool indices and attribute order stable. Unchanged methods retain their original Code bytes, including verifier metadata, debug information, and unusual encodings.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The library target is `hippocafe::hippocafe`. Installed consumers use `find_package(HippoCafe CONFIG REQUIRED)` and link the same target. A library-only build can set `BUILD_TESTING=OFF` and `HIPPOCAFE_BUILD_EXAMPLES=OFF`.

The build fetches pinned minizip-ng and zlib-ng releases with CMake FetchContent. Test and fuzz builds also fetch pinned GoogleTest and FuzzTest dependencies. The `sanitized` preset uses Clang with ASan and UBSan. The `release-benchmarks` preset measures parsing, analysis, memory use, and writer throughput. Pass a Java home as the third benchmark argument to include runtime archive indexing and hierarchy lookups. See the [fixture manifest](tests/fixtures/README.md) for the supplied and generated test inputs.

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

## Edit a JAR or JMOD

`cafe::archive::Archive` opens an archive from a path or an owned byte vector. Use `Archive::create(Format::Jar)` or `Archive::create(Format::Jmod)` to start an empty editor. `entries()`, `find()`, and `read()` see staged edits immediately. Names are case-sensitive, slash-separated paths. Use `add` or `replace` with bytes, `add_file` or `replace_file` to copy a local file, and `erase` to remove an exact entry. `add_directory` adds an explicit directory entry with a trailing slash.

```cpp
#include <hippo/cafe.hpp>

using namespace cafe::archive;

auto jar = Archive::open("input.jar", Format::Jar);
if (!jar) {
  report(jar.error());
  return;
}
auto bytes = jar->read("example/Widget.class");
if (!bytes) {
  report(bytes.error());
  return;
}
auto parsed = cafe::read_class(std::move(*bytes));
if (!parsed) {
  report(parsed.error());
  return;
}
// Edit parsed->class_file here.
auto rewritten = cafe::write_class(parsed->class_file);
if (!rewritten) {
  report(rewritten.error());
  return;
}
if (auto replaced = jar->replace("example/Widget.class", rewritten->bytes); !replaced) {
  report(replaced.error());
  return;
}
if (auto saved = jar->save("output.jar"); !saved)
  report(saved.error());
```

`write()` returns archive bytes. `save()` publishes a completed archive and fails if the destination exists. Use `SaveMode::ReplaceExisting` to replace the destination atomically. Keep the contents of an opened source file stable for the lifetime of the editor. An atomic same-path save keeps the original file available through existing readers.

Retained entries keep their payloads, compression methods, timestamps, permissions, and comments. The writer regenerates ZIP headers and carries standard timestamp, ownership, and Unicode extra fields. New files default to Deflate, with Store available through `EntryOptions` and as a fallback when default compression would exceed a limit. `Limits` configures archive, staging, and decompression budgets and accepts a stop token.

Supply a JAR's manifest and any multi-release entries as ordinary files. To save edits to an opened signed JAR, explicitly erase its top-level signature files under `META-INF/` before saving. A JMOD needs `classes/module-info.class` and permits entries under `classes/`, `bin/`, `lib/`, `conf/`, `include/`, `legal/`, and `man/`. Supply the module descriptor and any associated module metadata as entry bytes.

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

`write_class` preserves the original encodings by default. `force_reencode` exercises the instruction encoder. In preservation mode a change to code or its layout requires fresh metadata from `CodeAttribute::supply_metadata` and the `CallerSuppliedMetadata` write policy. The supplied metadata covers the current instruction revision and the chosen output encoding. The caller supplies stack and local limits, handlers, and any required frames and offset-bearing attributes.

Use `erase_method(index)` and `erase_field(index)` to remove declarations. Later indices shift down, and the constant pool retains its slots. Update any code that refers to a removed declaration as part of the edit.

## Compute verification metadata

Build an explicit classpath-style resolution context with in-memory classes, class directories, JARs, and JMODs. `add_java_home` discovers JMODs in a modular JDK or runtime JARs in a legacy JDK. Register the runtime you are targeting. Call `set_target_release` to select multi-release JAR entries and enforce the corresponding class-version ceiling. Source order is in-memory classes, application dependencies, then runtime dependencies. The resolver lazily caches hierarchy metadata for that snapshot.

```cpp
cafe::classpath::ClassPathBuilder builder;
builder.set_target_release(21);
if (auto added = builder.add_java_home("/path/to/jdk-21"); !added) {
  report(added.error());
  return;
}
cafe::analysis::AnalysisContext context{
    cafe::classpath::ClassHierarchy{cafe::classpath::ClassResolver{builder.build()}},
};
auto output = cafe::write_class(
    parsed->class_file, {.code_analysis = cafe::CodeAnalysisMode::ComputeFrames}, context);
if (!output) {
  report(output.error());
  return;
}
save(output->bytes);
```

`build_cfg`, `compute_max_locals`, `compute_max_stack`, and `compute_frames` are also available directly under `cafe::analysis`. A CFG result can check its instruction revision and exception table with `is_current`. The frame result's owner, method, and context overload also checks the class declaration and hierarchy snapshot.

`ComputeFrames` recomputes both limits and emits stack maps for modern class versions. It remaps debug and type annotation offsets on decoded Code and reports unsafe attributes. Set `drop_unsafe_code_attributes` to drop those attributes with a diagnostic. `ComputeMaxs` computes the limits without a hierarchy. When it reuses an existing stack map it preserves the declared bounds required by that table. Editing modern Code that needs a new stack map requires `ComputeFrames`.

The analysis profile covers major versions 45–69. Legacy subroutines and unreachable code that needs a modern frame return structured analysis errors. Reference joins with incomparable interfaces try a bounded set of candidate types and check subsequent instruction uses. When the candidates cannot satisfy the analyzed uses, the join reports a structured error. The target JVM still performs member linkage and loader and module access checks.

## Fuzz

```sh
cmake --preset fuzz
cmake --build --preset fuzz
build/fuzz/fuzz/hippocafe_fuzz --fuzz_for=10s
mkdir -p build/fuzz/fuzz/mutated
build/fuzz/fuzz/hippocafe_libfuzzer -seed=314159 -runs=5000 build/fuzz/fuzz/mutated build/fuzz/fuzz/seeds build/fuzz/fuzz/archive-seeds
```

The round-trip example is available as `hippocafe_roundtrip input.class output.class`. The structural inspection example is `hippocafe_inspect_class input.class`.
