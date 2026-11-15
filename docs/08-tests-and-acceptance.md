# 08 — Test Plan, Fuzzing, and Acceptance Gates

## Definition of done

Pass one is complete only when a class can be read, inspected and written without losing valid attributes or corrupting CP references, the compatible parser survives safely bounded malformed attributes, historical corpus samples can be handled according to declared support, and method code can be decoded/encoded without silently invalidating verification metadata.

Passing only unit tests with synthetic Hello World classes is **not** sufficient.

## Test suites

### A. Binary primitives

Test every endianness read/write, signed boundary, truncated read, empty span, exact length, zero-length sub-reader, offset near `SIZE_MAX`, and repeated `take_slice` calls. Verify no unaligned access UB and no offset arithmetic overflow.

### B. Constant pool

Cover every standardized tag and version gate. Include duplicate UTF-8 entries, unreferenced entries, Long/Double terminal edge cases, zero forbidden indexes, reference-to-wrong-tag, valid UTF-8 and Java modified UTF-8 special cases (`NUL`, supplementary characters represented by surrogate pairs), invalid bytes, float/double NaN payload preservation, dynamic constant/bootstrap links. Test CP append without renumbering any old index.

### C. Class/attribute envelopes

For every relevant context (class, field, method, Code; Record component once typed): unknown opaque attribute, zero-length attribute, valid typed attribute, malformed bounded typed payload, mismatch between decoded consumed length and declared length, invalid name CP index, nesting and duplicate attributes. Validate counts and length recalculation after dropping malformed content.

Special malicious fixture:

```text
Class version 45.x
SourceFile           [well formed]
RuntimeVisibleAnnotations [modern-known name, malformed modern payload,
                           but valid six-byte header and in-bounds length]
Deprecated           [well formed]
```

Expected: Compatible ignores/drops inapplicable annotation without calling annotation parser, emits a diagnostic, retains both other attributes and writes a well-framed output; Strict rejects it. A similar modern-version fixture with a malformed permitted annotation attribute should drop it in Compatible mode but fail in Strict mode. Test the same pattern on method and Code attribute lists where applicable.

Other must-have negatives: `attribute_length` longer than enclosing bytes (fatal), invented unknown CP tag (fatal), truncated attribute header (fatal), deeply nested annotation payload (resource limit), absurd element counts within small length (no huge allocation), malformed Code raw fallback versus fatal path, invalid branch to padding/middle of opcode, invalid handler range.

### D. Instruction decoding / encoding

Build a table-driven test for **every supported opcode** with at least one valid example and malformed operand variants. Include:

- All implicit and explicit local forms, normal and `wide`.
- `iinc` signed narrow/wide ranges, signed `bipush`/`sipush`.
- `ldc`, `ldc_w`, `ldc2_w`, each with proper CP tag checks.
- Conditional branches, `goto(_w)`, `jsr(_w)`, `ret`.
- Table/lookup switches at all four alignment positions; negative keys; sparse/dense tables; default labels; counts near limits.
- `invokeinterface` count and reserved zero; `invokedynamic` two reserved zero bytes.
- `newarray`, `multianewarray`; reserved/illegal opcode handling.
- Insert/delete/move sequences with randomly generated instruction ID handles, generation checks and label relocation rules.

A code encoder test must not be bypassed merely because the normal writer can return the original byte buffer. Force canonical re-encoding into a fresh buffer where safe, parse it again and compare the logical instruction stream and branch destinations.

### E. Historical regression corpus

Include legitimately distributable or locally generated `.class` fixtures spanning early standard 45.x files, a verified historical experimental variant (if a dialect adapter is implemented), classes with old subroutine opcodes (`jsr`/`ret`), and newer versions with modern attributes. Record fixture provenance and a SHA-256 digest in a manifest. Do not vendor third-party binaries without license/permission. For early alpha file formats, tag known incompatibilities with explicit expected error codes rather than treating wrong parsing as a pass.

Distinguish "parsed and written" from "runs on a modern JVM"; a historically valid old or malformed-but-recovered class may deliberately fail on a modern verifier.

### F. Real-world corpus

Exercise a corpus of classes compiled from ordinary Java source and from real JARs. Cover interfaces, annotations, enums, records, sealed classes, modules, invokedynamic lambdas, condy, try/catch/finally, debug info, multiple switch instructions, zero/large methods and unusual attribute order. `read -> write -> read` should preserve the class model and all intact opaque attributes. For valid inputs, optionally verify loading/behavior with an appropriate JVM version when available.

### G. Malformed-input fuzzing

- LibFuzzer/AFL-style harness: `read_class` on arbitrary bytes, with strict and compatible modes. No crashes, hangs, OOB, UB or unbounded allocation.
- Corpus-driven mutations: flip CP tags, corrupt attribute lengths, swap attribute names, change class versions, reduce `code_length`, alter switch counts, change branch offsets and truncate at every byte boundary.
- A second harness: if Compatible parsing returns a safely writeable class, `write_class`, then reparse output in Strict/Compatible mode as policy permits; never blindly write opaque essential Code.
- Run with ASan and UBSan in CI, configurable resource limits and deterministic seeds.

### H. Allocation / lifetime

No dangling `string_view`/raw span after input buffer destruction. Test `ClassFile` move construction, safe destruction ordering, repeated parser instances, repeated class read/write, stale slot handles, switch payload freelist reuse and out-of-line operand deletion. Use an allocation-counting upstream resource to ensure no allocation per trivial opcode in a pre-reserved method. Measure peak allocations and bytes for parsing large JAR-derived corpora.

## Acceptance matrix

| Case | Expected parse | Expected write | Required evidence |
|---|---|---|---|
| Valid standard class | Clean | Success | Round-trip and semantic comparison |
| Valid modern class with opaque standard attrs | Clean | Success if unchanged | All raw attrs unchanged |
| Valid unknown vendor attr | Clean | Success | Original raw payload and name retained |
| Known future attr on old class, bounded | Compatible: recovered | Success after drop | Diagnostic + correct attr count |
| Malformed nonessential attr, bounded | Compatible: recovered | Success after drop | Neighbor attrs survive |
| Malformed nonessential attr, bounded | Strict: error | Not applicable | Error at correct offset |
| Attr length exceeds enclosing bytes | Error in both | Not applicable | No boundary guess |
| Unknown CP tag | Error in both | Not applicable | Deterministic explicit error |
| Historical standard 45.x | Clean or documented recovered | Success when representable | Regression fixture |
| Experimental dialect without adapter | `UnsupportedDialect` or other clear error | Not applicable | No false success |
| Unmodified Code with frames/maxes | Clean | Success | Byte-identical `Code` payload |
| Dirty Code, stale frames/maxes | Parse as edited model | Default error | `NeedsCodeMetadata`/`StaleCodeMetadata` |
| Dirty Code with caller-supplied metadata | Explicit opt-in | Structurally valid emission or clear error | Layout and metadata checks |
| Malformed Code unable to decode | Error or explicit opaque essential recovery | Restricted | No fake decoded instructions |

## Performance goals (measure, do not promise a number)

Track parsing throughput, writing throughput, peak RSS, total allocation count, bytes retained per class, bytes per live instruction, and cost of repeated inserts. Include methods with few and many instructions, many tiny classes, switch-heavy obfuscated code and many unknown attributes. Establish baselines from the initial implementation; reject regressions on objective measurements rather than an assumed speedup multiplier.

## Required tooling

CTest test target(s), sanitizer preset, warning-enabled debug preset, release benchmark preset, a reproducible corpus manifest, and at least one fuzz target buildable on a supported toolchain. Failed parsing must leave no partial successful result, double free, or corrupted state in a reusable reader.
