# 16 — Maximum Stack/Locals and Safe Writer Integration

## Objective

Add explicit `max_locals`, `max_stack`, and `StackMapTable` computation to the existing class-file writer while preserving its core contract: unchanged methods may be emitted from original bytes, and **rewritten methods are never allowed to carry unexamined stale verification/offset metadata**.

Computation and emission are distinct. `max_stack`/`max_locals` count execution resources; frame analysis derives verifier type states; the writer decides physical constant-pool indices and instruction offsets. Every automatic path must be able to fail without partially modifying the caller's `ClassFile`.

## `max_locals` computation

Start from the method descriptor's argument slots, including slot 0 for `this` on instance methods. Examine each local-variable instruction, normalized or wide, and determine its highest occupied slot:

```text
required = parameter_slot_count
for local access (index, width):
    required = max(required, checked(index + width))
```

`long` and `double` loads/stores occupy two slots. `iinc` occupies one integer slot; legacy `ret`, if supported, occupies one return-address slot. Debug variable table ranges do **not** by themselves establish executable local-slot requirements. Explicitly reject indexes or total sizes outside the Code attribute/JVM limits; do not truncate to `uint16_t`.

Computing required slots is not proof that all accesses have valid initialized types. A separate full frame analysis validates type/state. For programmatically created methods, no arbitrary upper-padding beyond the required slots is necessary unless the caller explicitly requests a larger allocation.

**Tests:** static/instance method arguments, empty method body where code is valid, wide local indexes, `iinc`, overlapping long/double locals, invalid extreme indexes, and descriptor limit errors.

## `max_stack` computation

Track the maximum **occupied JVM operand-stack slots**, accounting for category-two values and exception-handler entry stacks. This is a CFG data-flow problem, not a linear sum of opcode deltas.

A hierarchy-independent stack-shape mode is desirable where all required widths and category forms are obtainable from opcodes, local-slot categories and field/method descriptors. It must still detect stack underflow, illegal `dup*` forms, incompatible join stack shapes, invalid terminal states and unbounded/inconsistent stack growth. A height-only analysis that ignores operand categories is not sufficient for general bytecode.

Where a shape-only analysis cannot safely determine behavior (for example unsupported subroutines or a verifier-sensitive type distinction), return a specific error or use the full simulator with an available hierarchy. Full frame computation also records maximum stack slots; the two modes must produce the same result for bytecode both successfully analyze.

Use exceptional entry stack depth 1 for reachable handler transitions. Ensure the peak at **every instruction boundary**, including immediately after a push in the middle of a basic block, is included. The result fits unsigned 16 bits, or the analysis fails.

## Writer modes

Extend the existing `WriteOptions`/`CodeWritePolicy` API without changing the preservation-first default:

```cpp
enum class CodeAnalysisMode {
    Preserve,       // unchanged original Code/valid caller metadata only
    ComputeMaxs,    // compute max_stack and max_locals; no new frames
    ComputeFrames,  // compute frames AND both maxima
};

struct WriteOptions {
    CodeAnalysisMode code_analysis = CodeAnalysisMode::Preserve;
    // Existing preservation/encoding/recovery options remain.
};
```

`write_class(class_file, options, analysis_context)` (or a matching overload) obtains an explicit classpath/hierarchy context when frame generation requires one. It must not silently load the host JDK. A missing context is an error only when required for the selected operation.

| Mode | Required behavior |
|---|---|
| Preserve | Reuse existing unchanged Code; reject edits with invalid/missing metadata, as already specified in `06` |
| ComputeMaxs | Recompute maxima; use only where existing frames remain valid or the target version legally needs none; otherwise reject with `NeedsFrameComputation` |
| ComputeFrames | Recompute maxima and semantic type states; replace obsolete StackMapTable where appropriate; handle all affected code-offset metadata or reject |

Calling `ComputeMaxs` does **not** automatically fix an edited class version that needs new stack maps. `ComputeFrames` does not imply that malformed or unverifiable input can be repaired by a guessed type join.

## Emission pipeline and layout dependencies

The physical bytecode layout affects switch padding, short/wide branches, CP index widths and `new` offsets in uninitialized types. Plan the entire write transaction before emitting it:

1. **Snapshot/validate:** confirm the class model, method/handler label bounds, version, and analysis context. Select original-Code fast path for genuinely unchanged methods whose existing metadata is still valid.
2. **Prepare emission IR:** for rewritten Code, construct a temporary layout plan from semantic instructions and labels. If a conditional branch needs an inverted-condition + `goto_w` trampoline, represent its **extra control-flow boundary and instruction(s)** in the plan before final frame analysis. A branch-relaxation rewrite must never be hidden solely in raw bytes after frames were computed.
3. **Allocate CP entries:** preserve original CP indexes and append required class constants for frames and any new references. Verify long/double reserved slots and CP u2 limits. Finalizing CP indexes before code layout fixes `ldc` width decisions.
4. **Analyze finalized semantics:** run CFG, maxima, frame propagation and validation on the actual emission plan, including any synthetic branches, unless an unchanged method is explicitly preserved. Record the plan's modification/layout version.
5. **Determine physical layout:** choose legal encodings, resolve labels, branch widths and switch padding to convergence. If layout discovers an additional *semantic* branch expansion, return to step 2/4 and recompute affected frames; ordinary width/padding changes need only offset relocation.
6. **Materialize offsets:** map every instruction boundary and allocation-site `new` to final byte offsets; encode exception-table boundaries and supported code-offset-bearing attributes from their stable labels/ranges.
7. **Encode metadata:** generate `max_stack`, `max_locals`, and a single valid `StackMapTable` (when required) using the finalized offsets and CP entries.
8. **Validate and commit:** check code length, attribute lengths/counts, frame offsets, permitted verification tags, CP references, and all raw attribute dependency rules. Only then return a new byte vector; do not mutate the caller on failure.

A fixed-point layout algorithm needs checked displacement arithmetic and a convergence guard. Wider branch forms and alignment changes must not silently change semantics. Do not assume `max_stack` or frame type states are tied to original instruction **byte offsets**; they are tied to semantic instruction/label identities until the writer finishes layout.

## StackMapTable encoding

The first frame at method entry is **implicit**. For explicit entries, let `offset` be the final byte offset of the applicable instruction, and `previous_offset` the last explicit frame position (except the implicit initial-frame case):

- First explicit frame: `offset_delta = offset`.
- Later explicit frame: `offset_delta = offset - previous_offset - 1`.

Values must be nonnegative, u2-representable where required, and strictly increasing by byte offset. Emit `full_frame` first if preferred for correctness; a later optimization may select `same_frame`, `same_locals_1_stack_item`, `append_frame`, `chop_frame`, and extended forms using **equivalent semantic frames**. Check trimming/trailing `Top`, category-two locals and wire entry counts carefully.

For a parsed method whose previous `StackMapTable` is still stored as an opaque/raw attribute, recognize it by a validated attribute name in the canonical ordered Code attribute list. `ComputeFrames` replaces that entry with the newly encoded table (or removes it if the applicable version/semantics require none). Reject duplicate or ambiguous tables under the selected policy; never append a second table while retaining the old one. Preservation mode continues to leave the original raw table intact when Code is unchanged.

A wire `Object_variable_info` holds a valid CP index of a `CONSTANT_Class` entry; an `Uninitialized_variable_info` holds the byte offset of the surviving corresponding `new` instruction. `Top`, `Integer`, `Float`, `Long`, `Double`, `Null`, `UninitializedThis` use their applicable defined tags. Do not write internal-only types or unresolved interface-join constraints. Do not embed raw `LabelId` or `TypeId` values on disk.

**Versions:** for versions prior to stack-map verification, do not manufacture a modern StackMapTable. For modern versions, omit the attribute only when an implicit empty table is actually sufficient for the method under the spec; otherwise encode the required frames. The old optional inference fallback for version 50 is not a generation strategy.

## Offset-bearing Code attributes and exception tables

Rewriting Code can invalidate all of:

- Exception-table `start_pc`, `end_pc`, `handler_pc`.
- `StackMapTable` frame offsets and `uninitialized` allocation-site offsets.
- `LineNumberTable`, `LocalVariableTable`, `LocalVariableTypeTable` offsets/ranges.
- Code-targeted runtime type annotations and other standard offset-bearing nested attributes.
- Unknown opaque Code attributes that may encode undocumented offsets.

Use the stable label/boundary mapping for every supported typed attribute. If a referenced instruction/range was deleted, offer an explicit caller policy (drop/rebind when semantically justified) or error; never guess an offset. Preserve attribute order and avoid duplicate StackMapTable entries when replacing one.

**Opaque Code attributes:** preserving original constant-pool indexes is insufficient if code offsets changed. The default writer must reject modified Code with potentially offset-bearing opaque nested attributes unless the caller explicitly drops them or a documented known-safe preservation rule applies. This remains true even if computed frames are correct.

If compatibility parsing previously dropped a malformed nested Code attribute, preserving original `code[]` bytes and rebuilding just the attribute envelope remains valid without recomputation when instruction layout and all surviving offset dependencies are unchanged.

## Error behavior

Errors should include `NeedsFrameComputation`, `MissingAnalysisContext`, `StaleAnalysisResult`, `ClassResolutionFailure`, `InvalidStackShape`, `InvalidVerificationFrame`, `InvalidLabelOrRange`, `UnsafeOpaqueCodeAttribute`, `UnmappedCodeOffset`, `CodeTooLarge`, `ConstantPoolOverflow`, `StackMapOffsetOverflow`, `UnrepresentableFrame`, and `UnsupportedSubroutineAnalysis`.

An encoded method is not necessarily verifiable. Reject rather than emit unverifiable output. Do not automatically suppress errors by downgrading the class-file version, stripping unrelated attributes, raising declared maxima arbitrarily, or substituting Object for unresolved class types.

## Tests

- New/reordered instructions affecting branch offsets and switch padding at every alignment.
- `ldc` becoming `ldc_w` due to CP growth; `goto` widening; overflowing conditional branches requiring a safe expansion/error.
- `new` instruction shifted after edits; serialized `uninitialized` offset follows its new location.
- Handler and debug ranges remapped with labels; deleted/unmappable ranges rejected.
- Edited modern method with old frames: Preserve and ComputeMaxs must reject when stale, ComputeFrames must produce verifiable metadata.
- Frame compression (if implemented) and `full_frame` encoding yield equivalent verifier behavior.
- Writer output verified in a real JVM under a matching target runtime, not merely reparsed by this library.

## References

JVMS §4.7.3 (Code maxima and exception table), §4.7.4 (StackMapTable wire format and offset deltas), §4.9–§4.10 (verification), §6 (branch and switch encodings). `06-writer-and-roundtrip.md` remains normative for raw attribute and CP index preservation.
