# 09 — Implementation Plan and Agent Review Checklist

## Delivery approach

Implement each milestone as a coherent working slice. Keep the repository compilable, run tests after each gate, and avoid parallelizing tasks that assume unsettled ownership or error contracts. Complete the reader/writer fidelity core before adding optional ergonomic APIs.

### Gate 1 — Project skeleton and basic invariants

**Implement:** CMake23 library, public headers, `cafe` namespace, PascalCase/snake_case naming, read/write error and diagnostic types, checked big-endian binary reader/writer, `ReadOptions` limits. Define `ClassVersion`, `CpIndex`, `InstructionId`, `LabelId`, `SwitchId`. Install/export a usable target.

**Prove:** build on chosen supported compilers; binary-reader truncation and offset-overflow tests; sample public API compiles; no nonstandard runtime dependencies required.

### Gate 2 — Allocation and constant-pool ownership

**Implement:** `ClassStorage` stable heap address, class-owned input bytes, PMR resource ownership, append-only `ConstantPool`, raw UTF-8 preservation, typed CP entries for all known tags, long/double padding slots, lazy name accessors.

**Prove:** CP round-trip for every tag; duplicates and indices never change; float/double bits survive; input buffer can be freed; move construction and destruction sanitizer tests pass. No accidental CP compaction.

### Gate 3 — Class and attribute envelopes

**Implement:** `ClassFile`, `Field`, `Method`, ordered attribute representation, version-sensitive attribute registry, fully bounded `AttributeReader`, typed simple attributes and raw opaque fallback, transactional recovery, diagnostics. Initialize a complete class envelope without Code decoding yet.

**Prove:** class/field/method attr order intact; valid unknown attr preserved; malformed optional attribute dropped without damaging neighbor; future-known attr on old version skipped by policy; unknown CP tag and impossible length fail correctly.

### Gate 4 — MethodCode and instruction decoder

**Implement:** 8-byte `Instruction`, opcode metadata, slot-map-backed `MethodCode`, label boundary mapping, TableSwitch/LookupSwitch storage, decoding every specified opcode form, original code bytes and dirty/invalidation distinction, exception handler table.

**Prove:** opcode fixture matrix, switch padding, wide decoding, old `jsr`/`ret`, bad branch recovery, generation-ID tests, no per-instruction allocations once reserved.

### Gate 5 — Attribute integration and source preservation

**Implement:** typed `Code`, `Exceptions`, `BootstrapMethods`, simple attributes; nesting/opaque preservation of all other standard attributes; class-owned slices, recovery of bad nested nonessential attributes without invalidating Code offsets; cross-reference validation.

**Prove:** read class fixtures from old and new specifications, preservation of raw StackMapTable and unknown Code attributes, correct recovery from malformed nested debug info, no partial parent mutation. Run sanitizer-enabled fuzz smoke tests.

### Gate 6 — Writer

**Implement:** index-preserving CP emitter, class/member/attr serializer, original Code fast path, conditional Code reconstruction after nested attr drops, independent `CodeEncoder` and layout pass, writer error policies for stale verification metadata and opaque offsets.

**Prove:** clean read–write, recovered attribute write with corrected counts, cp-index stability, all opcode encoder fixtures, branch bounds, conditional overflow errors (or documented safe widening), switch alignment and 65535-byte code size enforcement.

### Gate 7 — Real-world and adversarial validation

**Implement:** regression fixture corpus and manifest, strict/compatible matrix, malformed attribute corpus, historical early-class compatibility adapters only if fixtures demonstrate need, ASan/UBSan harnesses, repeatable microbenchmarks, readme/examples.

**Prove:** acceptance matrix in `08-tests-and-acceptance.md` passes. Demonstrate a well-formed old class and a poisoned older class read/write (with diagnostics and dropped inapplicable attribute), and a modern valid class preserving StackMapTable and all opaque metadata.

## API correctness review

Before declaring any gate complete, inspect:

- Does any public call expose a pointer/reference that would be invalidated by vector growth without documented semantics?
- Can an instruction refer to a CP entry from the wrong class or a label from another method? How is misuse detected?
- Does a method have only one canonical Code owner and an ordered attribute list?
- Are raw slices backed by class-owned storage and checked before use?
- Are all `std::pmr` resources still alive when destructors of contained objects run?
- Does Compatible recovery issue a diagnostic for **every** dropped known attribute?
- Is there a version gate **before** invoking a codec for a known attribute?
- Can any failure in a nested decoder leave partially committed state?
- Can the writer emit stale code-offset data or stale max/frame metadata without explicit caller approval?
- Does the writer retain duplicate CP entries, unknown attributes and exact MUTF-8 bytes?
- Are all counters and lengths checked against JVM wire-width limits?
- Are historical behavior claims backed by fixture tests rather than guesswork?

## Do not spend this pass on

- Recomputing `max_stack` or `max_locals`.
- Constructing/generating `StackMapTable` frames.
- Control-flow graphs, dominator trees, SSA, class hierarchy or verifier analysis.
- Automatic method obfuscation/deobfuscation passes.
- Constant-pool compaction, layout-based optimizations, or whole-program symbol interning.
- Designing a custom allocator before measuring the PMR baseline.
- Adding untested recovery heuristics that scan for magic numbers or guessed attribute boundaries.

## Concrete demonstration scenario

The implementation agent must be able to show this flow:

```cpp
std::vector<std::byte> bytes = load_class_file("Example.class");
auto parsed = cafe::read_class(std::move(bytes));
if (!parsed) return report(parsed.error());

for (const auto& warning : parsed->diagnostics)
    report(warning);

const auto output = cafe::write_class(parsed->class_file);
if (!output) return report(output.error());

save_class_file("Example-roundtrip.class", output->bytes);
```

A companion fixture must be an old-format class whose known newer-version attribute has an intentionally corrupt *payload* but a valid `attribute_length`. The compatible parser must keep neighboring data intact, issue a drop diagnostic and write an internally well-framed class. A second fixture with a corrupt attribute length that cannot be safely bounded must report a fatal error.

The completion report must state which attribute families are typed, which are deliberately opaque, which historical versions/dialects were fixture-tested, which writer policies are enforced, the corpus/fuzz results and the measured performance baseline. Never claim complete semantic support for opaque features.
