# 06 — ClassWriter, Fidelity, and Safe Emission

## Writer contract

The writer emits a coherent class-file container from the retained model. It does **not** act as a verifier and must not silently recompute stack-map frames or maximum stack/local counts. It must not silently preserve stale verification structures after code edits.

```cpp
enum class CodeWritePolicy {
    RequireUnchangedOrSafe,   // default; preserve original Code bytes
    CallerSuppliedMetadata    // explicit advanced path; never presumed valid
};

struct WriteOptions {
    CodeWritePolicy code_policy = CodeWritePolicy::RequireUnchangedOrSafe;
    bool preserve_original_encodings = true;
};
```

`WriteResult` contains output bytes and diagnostics. `WriteError` identifies exact method/attribute when the operation cannot be completed safely. Writer options should never allow out-of-bounds sizes or unchecked CP indices; a 'caller supplied' mode relaxes only the *absence of analysis*, not basic structural checks.

## Index-preserving constant pool strategy

**Do not rebuild or compact the CP in pass one.** Emit original constant-pool entries at their original indices, preserving duplicate/unused entries, exact MUTF-8 wire bytes and exact float/double payload bits. New entries are appended. Every surviving parsed attribute (typed or raw), original Code body and old CP reference can therefore continue to use its original index.

- `constant_pool_count` is `slots.size()` including slot 0 and Long/Double reserved slots; ensure it fits u2.
- Encode all known CP tags with their defined fields and version-sensitive legality rules. No guessing for unknown tag payloads.
- `CONSTANT_Dynamic`/`InvokeDynamic` reference bootstrap method indices, not CP indices. Preserve bootstrap ordering and indices; append new entries deliberately.
- No automatic deduplication of original pool entries; optional deduplication only for new appended values.
- Do not optimize strings, renumber CP references, or canonicalize NaNs as a side effect of writing.

This preservation-first design is a **deliberate pass-one tradeoff**. A compacting writer may be implemented later only after every known and opaque CP consumer is handled or explicitly rejected.

## Attribute write behavior

Emit attributes in their model order and compute fresh counts and lengths from the attributes actually retained. For each attribute:

1. Its name refers to an existing `CONSTANT_Utf8` index; verify it.
2. Typed payloads encode into a temporary buffer or pre-measure with checked arithmetic.
3. Raw payloads copy the exact bytes from their class-owned slice or owned buffer.
4. A dropped attribute does not appear and is not counted. Its read-time diagnostic must remain available to the caller.
5. Unknown or opaque payloads are preserved **only** when their index/offset dependencies remain valid. Preserving original CP indices satisfies CP dependencies, but not code-layout dependencies.
6. A writer must not invent a wrong attribute version, location, or structure to fill missing data.

**Important:** preserving an unknown attribute with a CP index does not guarantee a JVM understands it; the round-trip guarantee is faithful retention of its payload and framing, not automatic semantic validation of private/vendor formats.

## Code output: two explicit paths

### Path A — unchanged Code (default and primary pass-one requirement)

If a method's Code and all of its offset-bearing semantics are unchanged, copy the original `Code` payload bytes (or at minimum the original encoded code array and opaque nested attributes) rather than re-layout. This retains:

- the exact bytecode encodings, including `wide`, `ldc` variants and switch padding;
- original branch displacements and exception-table offsets;
- original `max_stack`, `max_locals` and `StackMapTable` data;
- `LineNumberTable`, local-variable ranges and code-targeted annotation offsets;
- unknown Code attributes that might encode their own offsets.

Appending new CP entries or modifying a class-level attribute does not invalidate this fast path as long as existing CP indices remain stable.

**Note:** when recovery dropped nested malformed Code attributes, the complete original Code payload cannot be copied: rebuild the enclosing Code attribute from its parsed code bytes, exception table and retained nested attributes while preserving byte offsets. The dirty signal must distinguish *logical code bytes/layout changed* from *nested attribute set changed*. This distinction is necessary to remove a bad attribute without triggering frame computation.

### Path B — Code re-encoding (must be implemented, but guarded)

`CodeEncoder` walks ordered instructions and labels, computes offsets, chooses valid encodings and writes new Code bytes. It must handle all recognized operands including branch offsets and switch padding. It validates code length, CP operand index widths, instruction-local index widths, signed immediates, array dimensions, switch count/order and exception-table boundaries.

A code edit that changes byte offsets can invalidate `StackMapTable`, line tables, local-variable ranges, exception bounds, type annotations, and unknown nested Code attributes. Without analysis, default writing **must fail** with `NeedsCodeMetadata`/`StaleCodeMetadata`, rather than copy these structures blindly. A caller-supplied-metadata mode is permitted only if the caller explicitly supplies or updates `max_stack`, `max_locals`, necessary frame data, and every affected offset-bearing attribute (or removes those that can safely be omitted). Unknown code-offset-bearing attributes must be dropped explicitly or make the writer reject the operation. The library must never infer that a stack-map table is valid merely because it can encode instructions.

For a newly constructed method, allow caller-supplied metadata and a coherent attribute set. Re-encoding unchanged decoded methods should be available as a test/diagnostic path even when the normal writer prefers raw preservation; such a re-encode must leave metadata positions unchanged or be treated under the same stale-metadata rules.

## Code layout algorithm

1. Build the ordered list of instruction nodes and stable label-boundary positions.
2. Choose initial shortest legal opcode encodings, honoring preservation hints only when legal.
3. Calculate instruction start offsets and switch padding based on the next operand's 4-byte alignment relative to the start of the code array.
4. Resolve branch displacements **relative to the branch opcode**; widen `goto`/`jsr` as necessary or use correctly encoded wide variants.
5. Conditional branch overflow: either implement inverted-branch + `goto_w` trampoline with a documented instruction mapping and metadata update, or return `BranchOutOfRange`. Never truncate/overflow.
6. Repeat size/offset computation if widening or conditional expansion changes layout or switch padding, until stable with a guard against nonconvergence.
7. Validate all labels, exception ranges, code length <= 65535 bytes, CP refs, and byte-format limits.
8. Emit bytes into a checked growable output buffer; never patch outside it.

`tableswitch` size is `1 + padding + 12 + 4 * n`. `lookupswitch` size is `1 + padding + 8 + 8 * n`. Switch default and case branch displacements are signed 32-bit offsets relative to the switch opcode start.

## How to recover and write malformed attributes

Consider a class with three attributes: valid `SourceFile`, malformed newer-version `RuntimeVisibleAnnotations`, valid `Deprecated`. Compatible reading drops the inapplicable/malformed middle attribute. On writing, the class has **two** attributes; the count and sizes are correct, unrelated members and bytecode are preserved. The result may be byte-different, but the recovery is explicit and safe.

If an optional `LineNumberTable` is malformed, the reader can drop only that nested attribute; writing rebuilds Code's attributes table with the unchanged `code[]`, exception handlers and other nested attributes. This does not require recomputing `max_stack`/`max_locals` or frames. If `Code` itself is malformed or undecodable, preserve it only under an explicit opaque-essential policy and restrict emission accordingly; do not claim the repaired class is valid without evidence.

## Writer errors (minimum set)

`InvalidClassModel`, `InvalidConstantPoolReference`, `ConstantPoolOverflow`, `AttributeTooLarge`, `CodeTooLarge`, `BranchOutOfRange`, `InvalidSwitchTargets`, `InvalidLabel`, `StaleCodeMetadata`, `NeedsCodeMetadata`, `UnsafeOpaqueAttribute`, `UnsupportedOpaqueCode`, `UnsupportedDialect`, `ResourceLimitExceeded`.

## Fidelity goals and non-goals

**Required:** a parsed, clean, unmodified class remains semantically equivalent after writing. All recognized attributes and unknown bounded attributes remain present in original order unless the explicit read policy dropped them. Preserve original CP indices, raw attr bodies and original Code bytes in the default path.

**Preferred:** byte-identical read–write for untouched clean inputs. A byte-for-byte fast path returning the original owned buffer is allowed if the model has no modifications or recoveries, but it must not mask parser bugs or skip the independent encoder tests.

**Explicitly not promised:** executing malformed classes, automatically repairing verifier errors, support for unknown CP tag encodings, re-encoding arbitrary modified code without corresponding metadata, or retaining opaque Code offsets after instruction-layout changes.

## Useful tests

- `read -> write -> read` for class files with ordinary and modern attributes.
- Byte-identical fast path for clean fixture and output diff explanation for recovered fixture.
- CP index retention with duplicate Utf8 entries, unused entries, NaN bits, Long/Double slots.
- Unknown raw attribute with embedded-looking CP index stays intact after appending new constants.
- Invalid attribute removed and `attributes_count` corrected without touching Code bytes.
- Table/lookup switch alignment at all four possible positions.
- Branch displacement exactly at signed 16-bit boundaries; invalid overflows rejected or safely expanded.
- Dirty code with unchanged `StackMapTable` rejected by default; explicit caller metadata option tested with validation.
- `max_stack`, `max_locals`, frames and exception tables of unmodified Code retained exactly.
