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

## Inspect instructions

Iterate `file.instructions(method_index)` to read instructions in bytecode order.
Each view carries its class context and stable `InstructionId`. `is()` compares the
logical opcode, `immediate()` reads a signed `bipush` or `sipush` operand, and
`local_index()` reads the slot for a local load or store. Compact and wide local
encodings yield the same logical opcode and slot.

<!-- snippet: examples/inspect_class.cpp:scan-method -->
```cpp
std::expected<void, cafe::CodeEditError> scan_method(const cafe::ClassFile& file, std::size_t method_index) {
  using cafe::Opcode;
  for (auto insn : file.instructions(method_index)) {
    if (insn.is(Opcode::Bipush) && insn.immediate() == 10)
      std::cout << "  bipush 10\n";
    if (!insn.is_invoke())
      continue;
    auto call = insn.method_invocation();
    if (!call)
      return std::unexpected(call.error());
    if (insn.is(Opcode::InvokeStatic) && call->matches("java/lang/Math", "abs", "(I)I"))
      std::cout << "  invokestatic Math.abs(int)\n";
    if (insn.is(Opcode::InvokeVirtual) && call->matches("java/io/PrintStream", "println", "(Ljava/lang/String;)V"))
      std::cout << "  invokevirtual PrintStream.println(String)\n";
  }
  return {};
}
```
<!-- end snippet -->

The direct range is empty for an out-of-range method index, absent or opaque
Code, or an empty body. Use `checked_instructions(method_index)` to distinguish
`InvalidMethod` and `NoCode` from a successful lookup of an empty body.
`is_invoke()` covers virtual, special, static, and interface calls.
`method_invocation()` and `field_access()` return a typed operand or a
`CodeEditError`. `WrongOperandForm` identifies an unrelated opcode and
`InvalidOperand` identifies a malformed reference. `MethodInvocation::matches`
compares the encoded owner, name, and descriptor exactly. Use
`inspect_instruction(insn.location())` for bootstrap-aware dynamic operands.

Keep the class alive while inspecting borrowed operands. A copied view describes
the instruction as observed. Reacquire the range after editing the code or
changing the method table. Save `insn.id()` to anchor a later edit in the same
method. IDs survive unrelated insertions and become invalid when their
instruction is erased.

## Match instruction sequences

Use `cafe::pattern` to search one decoded method in linear instruction order.
`opcode` and `any` match one instruction. `iload`, `istore`, and
`int_constant` accept an optional slot or signed 32-bit value. Integer constants
include `iconst_m1` through `iconst_5`, immediate pushes, and integer pool loads.
`invoke` and `field` match symbolic references using the filters you supply.

<!-- snippet: examples/inspect_class.cpp:capture-prefix -->
```cpp
std::expected<void, cafe::pattern::MatchError> show_prefixes(const cafe::ClassFile& file, std::size_t method_index) {
  namespace p = cafe::pattern;
  auto pattern = p::seq(
      p::capture("prefix", p::repeat(p::any(), 1, 3)),
      p::invoke({.opcode = cafe::Opcode::InvokeStatic, .owner = "java/lang/Math", .name = "abs", .descriptor = "(I)I"}));
  auto found = p::find_all(file, method_index, pattern);
  if (!found)
    return std::unexpected(found.error());
  for (const auto& match : *found) {
    std::cout << "  Math.abs prefix at instruction " << match.start_index() << '\n';
    if (auto prefix = match.capture("prefix")) {
      for (auto insn : prefix->instructions())
        std::cout << "    " << cafe::format(insn) << '\n';
    }
  }
  return {};
}
```
<!-- end snippet -->

`seq` matches consecutive patterns, `either` tries alternatives in declaration
order, and `optional` and bounded `repeat` consume greedily while allowing the
following pattern to match. `find_first` returns the first match, or an empty
optional. `find_all` returns non-overlapping matches by default. Pass
`MatchOptions{.overlap = true}` to consider every start position. Both searches
return errors for malformed patterns, invalid candidate operands, or exhausted work
limits.

Every reported match consumes at least one instruction. `start_index()` is the
zero-based position at search time, `size()` is the instruction count, and
`start()` is the stable handle used by the editor. A named capture that
participated can contain zero instructions. A missing or nonparticipating name
returns `std::nullopt`. In a repetition, the capture exposes its last
participating iteration. Captures retain the instruction views observed during
the search. Keep their source class alive while inspecting them.

## Edit instruction sequences

Use `ClassFile::edit_method(index)` to insert raw or symbolic instructions
relative to a stable anchor. `insert_before` and `insert_after` accept a single
instruction, an initializer list, a standard range of `MethodInstruction`, or a
builder. `cafe::insn` factories construct instructions with deferred symbolic
operands. This edit pushes an integer, calls `Math.abs(int)`, and discards the
result, leaving the original operand stack intact.

<!-- snippet: examples/edit_and_compute.cpp:edit-block -->
```cpp
std::expected<void, cafe::BlockEditError> insert_abs(cafe::MethodEditor& editor, cafe::InstructionId anchor) {
  namespace i = cafe::insn;
  auto inserted = editor.insert_before(
      anchor, {i::bipush(10), i::invokestatic("java/lang/Math", "abs", "(I)I"), i::pop()});
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
```
<!-- end snippet -->

A single insertion returns an instruction ID. A sequence returns IDs in the
order supplied, or a `BlockEditError` whose `instruction_index` identifies the
failing element when known. A failed edit preserves instruction order, labels,
constants, and bootstrap entries. An empty sequence at a valid anchor succeeds
with an empty ID list. Keep pool indices across edits and reacquire borrowed pool
entry pointers and class attribute spans after a symbolic edit attempt, since
temporary growth can relocate their storage. Both single-instruction and
sequence edits validate raw pool references against the class and require bound
executable branch targets. Use a builder-local label for a forward branch
within a sequence.

This example measures each non-null string passed to
`PrintStream.println(String)` and discards the length. Both paths leave the
receiver and original argument ready for the call.

<!-- snippet: examples/edit_and_compute.cpp:edit-matches -->
```cpp
std::expected<void, std::string> measure_printed_strings(cafe::ClassFile& file, std::size_t method_index) {
  namespace p = cafe::pattern;
  const p::InvokePattern println{
      .opcode = cafe::Opcode::InvokeVirtual,
      .owner = "java/io/PrintStream",
      .name = "println",
      .descriptor = "(Ljava/lang/String;)V",
  };
  auto calls = p::find_all(file, method_index, p::invoke(println));
  if (!calls)
    return std::unexpected(std::string(calls.error().detail));

  auto editor = file.edit_method(method_index);
  if (!editor)
    return std::unexpected("method index out of range");
  for (const auto& match : *calls) {
    auto inserted = editor->insert_before(
        match.start(),
        [](auto& code) {
          auto skip = code.label();
          code.dup().ifnull(skip);
          code.dup().invokevirtual("java/lang/String", "length", "()I").pop();
          code.bind(skip);
        },
        cafe::InsertionEntry::IncludeIncomingBranches);
    if (!inserted)
      return std::unexpected("insertion failed at instruction " + std::to_string(match.start_index()) +
                             " (edit error " + std::to_string(static_cast<int>(inserted.error().cause)) + ")");
  }
  return {};
}
```
<!-- end snippet -->

A callback receives a temporary `CodeBuilder`. Its `label()` and `bind()`
functions identify positions in the pending sequence. Binding `skip` at the end
of the inserted sequence makes it land on the original call. A branch at the
end of a method requires an executable destination. Unbound or foreign labels
cause the edit to fail. Exceptions from callback code propagate before the edit
is committed. Build a reusable sequence by constructing `cafe::CodeBuilder`
directly, then passing it to the editor. The builder owns its pending operands.
Its `tableswitch` and `lookupswitch` methods accept builder-local destinations
for reusable sequences. They also accept existing method-scoped `LabelId`
destinations, as do the switch factories.
For a bare `MethodInstruction` or range, keep any borrowed strings, bootstrap
arguments, and switch data alive through the edit.

The default `InsertionEntry::PreserveAnchor` keeps existing branches and switch
edges pointed at the original anchor. With `IncludeIncomingBranches`, incoming
branches and switch arms targeting the anchor execute the insertion first.
Exception-handler boundaries keep their original bindings.

Use `editor.replace(match, sequence)` or `editor.erase(match)` to edit a whole
match. The editor checks that its members still exist, are unchanged, and are
consecutive in the same method. A nonempty replacement retains the first
instruction's ID and entry labels. The editor rejects removals that would
orphan a bound label. Re-run the search to obtain a fresh match after a change
to any of its members, including a rebind of a branch or switch destination.
The editor checks a saved match before invoking a replacement callback and
again after the callback returns. Select a metadata policy when writing edited code.

### Work with raw method code

For low-level assembly, `MethodCode::instructions()` yields raw instruction
values paired with stable IDs. `MethodCode::append`, `insert_before`, and
`insert_at` accept an `InstructionBuilder` or a contiguous range of builders.
Labels from `make_label()` can be bound later with `bind_label()` to construct
forward branches. `insert_at` moves its selected label to the first inserted
instruction. Reacquire borrowed ranges and switch views after mutation. The
writer chooses implicit, explicit, and wide encodings.

### Add symbolic field, method, and dynamic instructions

Use `ClassFile::edit_method(index)` to get a `MethodEditor`. Its `append`,
`insert_before`, `insert_at`, and `replace` accept symbolic operands alongside
ordinary instructions, branches, and switches. Supply JVM internal owner names and descriptors.
The editor resolves and reuses constants and derives the `invokeinterface` slot
count. Use `owner_is_interface = true` for static and special calls to interfaces.

```cpp
using namespace cafe;

// With an int already on the stack, call Math.abs(int).
auto call = editor.append(MethodInvocation{Opcode::InvokeStatic, "java/lang/Math", "abs", "(I)I"});
auto field = editor.append(FieldAccess{Opcode::GetStatic, "java/lang/Integer", "MAX_VALUE", "I"});
```

A `DynamicInvocation` carries a call-site name and method descriptor. A
`DynamicConstant` carries a name and field descriptor, and the editor chooses
`ldc`, `ldc_w`, or `ldc2_w` during construction and serialization. Both take a
`Bootstrap` with a symbolic method handle and an optional span of static arguments.
For example, this adds a Java 11+ dynamic constant that resolves to a null String.

```cpp
const Bootstrap null_bootstrap{{
    MethodHandleKind::InvokeStatic, "java/lang/invoke/ConstantBootstraps", "nullConstant",
    "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;",
}};
auto load = editor.append(DynamicConstant{"value", "Ljava/lang/String;", null_bootstrap});
```

For a dynamic call site, this example concatenates two values already on the
stack using the JDK's string concatenation bootstrap. The recipe uses one
`\x01` placeholder per stack argument.

```cpp
const std::array args{BootstrapArgument{std::string_view("\x01: \x01")}};
const Bootstrap concat{{
    MethodHandleKind::InvokeStatic, "java/lang/invoke/StringConcatFactory", "makeConcatWithConstants",
    "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;"
    "Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/invoke/CallSite;",
}, args};
auto call_site = editor.append(DynamicInvocation{"concat", "(II)Ljava/lang/String;", concat});
```

Bootstrap arguments can be Java numeric constants, modified UTF-8 strings, class
literals, method types, method handles, and nested dynamic constants. The editor
extends the existing class-level bootstrap table where needed and keeps its
indices stable. Check each returned `std::expected`. A failed symbolic edit
leaves the code and class entries unchanged.

Use `editor.erase(id)` to remove an instruction identified by its handle. Retarget
any labels bound to it first. Erasure keeps existing pool and bootstrap indices
stable, since other instructions and attributes may refer to them. Supply the
stack operands and keep the surrounding stack usage valid when editing a method.

### Find and inspect symbolic instructions

`ClassFile::find_invocations(owner, name, descriptor)` and
`find_field_accesses(owner, name, descriptor)` search decoded method bodies in
instruction order. Pass a method index as the fourth argument to search one
method. Matching uses the exact owner, name, and descriptor encoded in the
class file. Each match contains a method index and an instruction handle for
`inspect_instruction` or `edit_method`.

To inspect an `invokedynamic` or dynamic constant load, pass its method index
and instruction handle to `inspect_instruction({method_index, id})`. The result's
`operand()` contains a `DynamicInvocation` or `DynamicConstant`, with the
bootstrap's symbolic `handle` and ordered `arguments`. Each argument's `value`
is a typed variant, including nested `DynamicConstant` values. The instruction
handle can come from `MethodCode::instructions()` or `MethodCode::at()`. Ordinary
loads and other unrelated opcodes return `WrongOperandForm`.

Keep the source class and the inspection result alive while using its operands.
You can pass `result->operand()` directly to a method editor. Inspection preserves
shared bootstrap rows and cycles. The editor rejects cyclic arguments on insertion
or replacement.

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

The complete example inserts the arithmetic sequence and instruments matched calls before computing frames.
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

## Edit a method in SSA

Call `cafe::analysis::lift_ssa` with a method index and the same hierarchy context
used for frame analysis. The returned graph owns its instructions, types and phi
inputs. Inspect `blocks()`, `operation()`, `value()`, and `uses()`. An operation's
inputs are in JVM evaluation order, with the receiver first for an instance call.
Saved IDs survive unrelated graph edits. Reacquire borrowed spans and pointers
after each edit. The context's upstream memory resource must outlive the graph.

The example swaps the two operands of the first integer subtraction. It edits
the graph, installs the verified method, then uses the ordinary class writer.

<!-- snippet: examples/edit_ssa.cpp:edit-ssa -->
```cpp
std::expected<cafe::WriteResult, std::string> reverse_difference(
    cafe::ClassFile& owner, std::size_t method, cafe::analysis::AnalysisContext& context) {
  auto graph = cafe::analysis::lift_ssa(owner, method, context);
  if (!graph)
    return std::unexpected(std::move(graph.error().detail));
  for (const auto& block : graph->blocks()) {
    for (const auto id : block.operations) {
      const auto* op = graph->operation(id);
      if (op->opcode != cafe::Opcode::ISub)
        continue;
      const auto left = op->inputs[0];
      const auto right = op->inputs[1];
      if (auto edit = graph->set_input(id, 0, right); !edit)
        return std::unexpected(std::move(edit.error().detail));
      if (auto edit = graph->set_input(id, 1, left); !edit)
        return std::unexpected(std::move(edit.error().detail));
      if (auto lowered = cafe::analysis::lower_ssa(owner, *graph, context); !lowered)
        return std::unexpected(std::move(lowered.error().detail));
      return cafe::write_class(owner).transform_error([](cafe::WriteError error) {
        return std::move(error.detail);
      });
    }
  }
  return std::unexpected("method contains no integer subtraction");
}
```
<!-- end snippet -->

`analyze_ssa(owner, graph, context)` verifies the graph and returns a read-only
snapshot. Use `uses(value)` on the snapshot to borrow the indexed operands,
phi inputs, and debugger assignments that consume a definition. The
`dominates` overloads compare block entries or test whether a definition is
available at an operation or on an incoming edge. An exceptional edge observes
the state before its protected operation produces a result. After a graph edit,
reacquire the snapshot and any borrowed use spans. `dump_ssa(graph)` prints
typed values, phi inputs keyed by edge, and exception sites using graph-local IDs.

Run `hippocafe_ssa_constants` to see a pass follow indexed uses through two
additions. The example in `examples/ssa_constants.cpp` records folds before
editing the graph, replaces both results, and checks the value returned by
the strictly reparsed method.

For a new body, first add a concrete method declaration to the class and call
`create_ssa`. Its entry block and descriptor-derived parameters are ready for use.
Append operations, terminate each block, and define its outgoing edges. A phi
takes one value per incoming edge, including distinct switch arms with the same
destination. `verify_ssa` checks the graph while leaving both it and the class
available for further editing.

`lower_ssa` replaces the selected method's instruction identities and prepares
metadata for `write_class(owner)`. Reacquire the code and instruction handles after
success. A failed lower preserves the previous body and class dependencies. Treat
a stale-source diagnostic as a request to lift the current method before applying
the edit again. Set `SsaLowerOptions::drop_unsafe_code_attributes` to permit an
unrelocatable Code attribute to be dropped and inspect the returned diagnostics.

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
