# Usage

Include `<hippo/cafe.hpp>` for the complete public API or individual `cafe/` headers
for narrower dependencies. Public operations return `std::expected` with domain-specific
errors; compatible read/write recoveries appear in diagnostic vectors. Inspect both.
General allocation failures can still throw `std::bad_alloc`.

## Read and write a class

`read_class(std::span<const std::byte>)` copies input once.
`read_class(std::vector<std::byte>&&)` transfers the buffer to the resulting `ClassFile`.
The class owns the storage referenced by its model. The default reader is compatible;
set `ReadOptions::mode` to `ParseMode::Strict` to reject optional-data recoveries.
See [compatibility](compatibility.md) for the distinction between a successful read,
faithful preservation, and JVM validity.

The `examples/quickstart.cpp` example contains this compiled function.
It reports read diagnostics and returns write diagnostics with the output bytes:

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

The caller saves `WriteResult::bytes` and handles its diagnostics. The complete
`examples/roundtrip.cpp` CLI checks file reads, writes, and close errors:
`hippocafe_roundtrip input.class output.class`. Use `hippocafe_inspect_class input.class`
for structural inspection.

## Inspect and edit declarations

Names and descriptors use JVM modified UTF-8. Class names use internal spelling,
such as `example/Widget`; method descriptors use forms such as `(I)Ljava/lang/String;`.
`constant_pool().get_utf8()` returns validated borrowed encoded bytes, while
`decode_utf8()` produces Java UTF-16 code units, including unpaired surrogates.

`find_method(name, descriptor)` and `find_field(name, descriptor)` return optional
current table indices. `edit_method(index)` borrows a `MethodEditor`;
`edit_code(index)` directly returns decoded Code or nullptr. Add/erase operations
can invalidate views into the corresponding member table; reacquire editors after
adding or erasing methods. Erasure shifts later indices and does not rewrite code
that references the deleted declaration.

Constant-pool appends preserve existing indices. Use `append_utf8()` for strings and
`append()` for fixed-width entries. A `CONSTANT_String` refers to a UTF-8 slot;
it is distinct from the UTF-8 slot itself. Existing pool entry pointers may be
invalidated by appends, so retain indices or value copies when editing.

## Edit instructions

Traverse `code.instructions()` to obtain value copies paired with stable handles.
Instruction IDs survive unrelated edits, but not erasure or owner destruction.
Labels identify instruction boundaries or the exclusive method end. New labels are
unbound until `bind_label()` supplies a position; this supports forward branches.
Branch and switch builders retain scoped labels until the method checks ownership.

`append`, `insert_before`, and `insert_at` accept one builder or an atomic block.
Pass an instruction directly for a single insertion; the result contains its
`InstructionId` or a `CodeEditError`. For multiple instructions, pass a contiguous
range of `InstructionBuilder` values, such as `std::array`; the result contains
the new IDs or a `BlockEditError`.
A failed block leaves logical code unchanged and can report the failing patch index.
A successful nonempty edit invalidates borrowed ranges and switch views.
`insert_before` leaves labels on the original anchor; `insert_at` moves only the
selected label to the first inserted instruction. Retarget labels before erasing
a labeled instruction. The writer chooses implicit, explicit, and wide encodings.

This helper inserts before an existing instruction, so the example does not create
an unreachable trailing instruction:

<!-- snippet: examples/edit_and_compute.cpp:edit-method -->
```cpp
std::expected<void, cafe::CodeEditError> insert_nop(cafe::MethodCode& code, cafe::InstructionId anchor) {
  auto inserted = code.insert_before(anchor, cafe::Instruction::nop());
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
```
<!-- end snippet -->

Use the block overload when several instructions must be inserted together. This
stack-neutral pair temporarily uses one stack slot, so its maxima and frames need
to be considered when writing:

<!-- snippet: examples/edit_and_compute.cpp:edit-block -->
```cpp
std::expected<void, cafe::BlockEditError> insert_push_pop(cafe::MethodCode& code, cafe::InstructionId anchor) {
  const std::array<cafe::InstructionBuilder, 2> patch{
      cafe::Instruction::bipush(42),
      cafe::Instruction::pop(),
  };
  auto inserted = code.insert_before(anchor, patch);
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
```
<!-- end snippet -->

After editing Code, select a metadata policy before writing.

## Select verification metadata policy

| Mode | Behavior |
|---|---|
| `CodeAnalysisMode::Preserve` | Default: preserve original Code and metadata when safe |
| `CodeAnalysisMode::ComputeMaxs` | Compute max stack/locals without complete reference-type verification |
| `CodeAnalysisMode::ComputeFrames` | Compute maxima and verifier frames for serialization |

`write_class(file, {.code_analysis = cafe::CodeAnalysisMode::ComputeMaxs})` needs no
hierarchy. Reusing an existing stack map can require retaining its declared bounds.
If maxima-only writing returns `NeedsFrameComputation`, use `ComputeFrames` to
regenerate the required frames.

For caller-provided metadata, use `CodeAttribute::supply_metadata()` with complete
limits, handlers, frames, and offset-bearing attributes for the selected encoding,
then select `CodeWritePolicy::CallerSuppliedMetadata`. Further code edits expire
that metadata. `force_reencode` exercises encoding even for unchanged methods;
it does not waive metadata safety checks.

## Write with computed frames

Use `write_class(file, options, context)` with
`options.code_analysis = CodeAnalysisMode::ComputeFrames`. The writer computes
max stack, max locals, and verifier frames for each decoded method and emits
`StackMapTable` where needed.

Register a runtime that matches the target Java release. `add_java_home()` supports
JDKs with `jmods/` and legacy installations with `rt.jar`. A linked `lib/modules`
image alone returns `UnsupportedRuntime`. Register dependencies with
`add_directory`, `add_jar`, `add_jmod`, or `add_class` before calling `build()`.
Frame analysis automatically adds the class being written to its hierarchy.

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

The complete example inserts a nop and a push/pop block before computing frames.
It reports diagnostics and saves the serialized class. This invocation targets Java 17.

```sh
hippocafe_edit_and_compute input.class output.class /path/to/jdk-17 17
```

If writing returns `ClassResolutionFailure`, check the error detail for the
unresolved class or member and check the registered runtime and dependencies.
Without an explicit context, the writer starts with an empty classpath and
reports `MissingAnalysisContext` when it cannot find a required class.

Frame computation remaps supported debug and type annotation offsets. The writer
rejects attributes it cannot safely relocate. Set `drop_unsafe_code_attributes`
to drop them with diagnostics. Frame computation requires decoded Code. To keep
opaque Code verbatim, use `Preserve` and enable `allow_opaque_code` separately
when reading and writing.

### Classpath lookup

`set_target_release(17)` selects manifest-enabled multi-release JAR definitions up to
Java 17 and enforces that class-version ceiling. Zero selects base entries without a
ceiling. Use a matching runtime because its class-file versions are checked against
the ceiling.

In-memory definitions take precedence, followed by application sources in registration
order and then runtime sources. `DuplicatePolicy::FirstWins` is the default.
`set_duplicate_policy()` can reject duplicates or collect shadow provenance.
Keep external source files stable for the snapshot lifetime and rebuild the snapshot
after external changes.

### Inspect a method's frames

Call `cafe::analysis::compute_frames(file, method, context)` to inspect a decoded
method's maxima and entry frames in memory. Use the writer workflow above to serialize
them. `build_cfg`, `compute_max_locals`, and `compute_max_stack` are also available in
`cafe::analysis`.

Retain the supplying PMR resource for as long as a returned graph or frame result
uses it. Check `is_current()` before reusing results after edits. The frame check
that takes the class, method, and context also checks the declaration and hierarchy
identities. See [compatibility](compatibility.md) for analysis limits.

## Edit JARs and JMODs

Use `Archive::open(path, Format::Jar)` or its owned-vector overload to index an archive.
Use `Archive::create(Format::Jar)` or `Format::Jmod` for an empty editor.
`entries()`, `find()`, and `read()` see staged changes immediately. Entry names are
exact, case-sensitive relative paths with slash separators.

`add` and `replace` copy byte spans. `add_file` and `replace_file` snapshot a regular
local file immediately. `add_directory` requires a trailing slash. `erase` removes
one exact entry without recursively removing descendants. Entry views are borrowed
until the next edit. This helper uses the read/write function above:

<!-- snippet: examples/quickstart.cpp:archive-edit -->
```cpp
std::expected<void, std::string> rewrite_entry(cafe::archive::Archive& archive, std::string_view name) {
  auto input = archive.read(name);
  if (!input)
    return std::unexpected(std::move(input.error().detail));
  auto output = roundtrip(std::move(*input));
  if (!output)
    return std::unexpected(std::move(output.error()));
  for (const auto& diagnostic : output->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  if (auto replaced = archive.replace(name, output->bytes); !replaced)
    return std::unexpected(std::move(replaced.error().detail));
  return {};
}
```
<!-- end snippet -->

`write()` returns complete archive bytes. `save(path)` publishes a completed archive
and fails if the destination exists; `SaveMode::ReplaceExisting` replaces it atomically.
Keep an opened source file's contents stable. Atomic same-path replacement preserves
the original file for existing readers.

New file entries default to Deflate; `EntryOptions` can select Store or permissions.
`Limits` bounds indexing, staged bytes, decompression, total output work, and path
sizes, and supplies cooperative cancellation. Unchanged entries retain their payloads
and supported metadata; ZIP headers are regenerated.

Supply manifests, versioned entries, and module metadata as ordinary files. Before
saving changes to an opened signed JAR, explicitly remove its top-level signature
artifacts under `META-INF/`. JMOD output requires `classes/module-info.class` and permits
entries under `classes/`, `bin/`, `lib/`, `conf/`, `include/`, `legal/`, and `man/`.
