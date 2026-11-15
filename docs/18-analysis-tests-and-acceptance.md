# 18 — Analysis Validation, Fixtures, Fuzzing, and Performance

## Acceptance principle

Success is **not** merely that an analyzed class can be serialized and reparsed by the same library. Successful frame generation requires independently checked JVM type-state correctness and valid metadata at final encoded offsets. Tests must cover normal compiler output, transformed methods, historical class files, and adversarial inputs.

Use the existing CMake/CTest and testing-dependency conventions; new analysis tests extend rather than replace parsing/writing tests. If GoogleTest and fuzzing dependencies are already provided through `FetchContent`, reuse those targets instead of creating a competing test framework or downloading a redundant copy.

## Test layers

### A. Archive and classpath tests

- JAR with stored/deflated classes, many entries, duplicates, unreadable member, missing class and malformed central directory.
- JMOD with a real wrapper, `classes/` prefix and non-class sections; reject arbitrary offset guesses.
- Class directory with nested packages; enforce path safety and test symlink escape policy.
- Multi-release JAR with/without a `Multi-Release: true` main manifest attribute and several applicable target versions. Include folded manifest lines, out-of-order entries and a malformed version directory.
- Explicit legacy `rt.jar`/ancillary runtime archives and a modular development kit with JMODs, where test environments provide them.
- Java home with no supported class archives: a useful error, not a silent fallback to the machine's default JDK.
- Ensure index registration does not eagerly inflate/decode every class body. Instrument metadata scans/decompression counts.
- Resolve duplicate names according to registered source precedence and strict duplicate policy.

### B. Hierarchy and type tests

Construct small, version-controlled Java fixtures that define:

- Deep class superclass chains, diamonds of interfaces, inherited implementations, covariant arrays and common supertypes.
- `java/lang/Object`, `java/lang/Cloneable`, `java/io/Serializable` and representative standard JDK types from an explicit runtime.
- Missing direct/transitive ancestor, interface cycle and superclass cycle as negative tests.
- Two incomparable common interfaces to test join ambiguity and downstream `invokeinterface` constraints.
- Arrays of every primitive kind, reference arrays, nested primitive/reference arrays and null type.
- A class whose same binary name exists in several classpath entries to test deterministic selection and cache invalidation.

Verify positive assignability against a real JVM where the selected hierarchy matches the JVM loading environment; verify negative/error paths without assuming missing classes are ordinary non-subtypes.

### C. CFG tests

- Straight-line, conditional branches, loops, irreducible branches, switches with repeated targets and backedges.
- Exception handlers spanning only parts of blocks, overlapping ranges and catch-all handlers.
- Handler state after a potentially throwing instruction vs before it; constructor invocation protected by a handler.
- Unreachable code and a reachable fallthrough past the end of Code.
- Invalid branch labels, deletion-induced invalid boundaries and stale CFG versions.
- Legacy `jsr`/`ret` must be reported unsupported unless an actual dedicated analyzer is present.

### D. Opcode simulator tests

A **table-driven coverage audit for each logical opcode** should record whether it is fully simulated for each supported class-file version or intentionally errors. No fallthrough default that treats unimplemented opcodes as no-ops. In particular include:

- Every legal and illegal category combination of `dup`, `dup_x1`, `dup_x2`, `dup2`, `dup2_x1`, `dup2_x2`, `pop`, `pop2`, and `swap`.
- Long/double loads, stores, local overlaps and stack counting.
- Primitive array operations, reference array covariance, null, casts and instance checks.
- Invokes (including `invokedynamic`), field descriptor widths, dynamic constants including two-slot types.
- Constructor delegation, object allocation aliases, initialization on normal vs exceptional successors.
- Wrong receiver type, incompatible return, invalid CP tag, malformed descriptor and illegal uninitialized uses.

### E. Frame and maximum computations

- Compare computed `max_locals` against a manually derived oracle for method descriptors and local instructions, including high/wide indexes and u2 boundaries.
- Compare `max_stack` against explicit stack-slot traces and an independent verifier for loops, nested handlers, category-two values, and stack-manipulation operations.
- Assert no extra stack slots are introduced solely by a phantom label or branch encoding hint.
- Verify frame merges at backedges, shared-interface joins, null/reference joins, primitive mismatches and local-unusable states.
- Verify constructor and exception pre-state rules with stack-map frames at handler entries.
- Explicitly check first implicit frame and later `offset_delta` off-by-one behavior, including consecutive frame offsets and large gaps.
- Check every generated `Uninitialized` wire offset points at a final-layout `new` instruction.
- Test unsupported subroutine analysis and unsupported future verifier features as structured failures.

### F. End-to-end rewritten methods

Prepare Java source plus compiled `.class` fixtures from more than one supported compiler/runtime configuration where available. For each fixture:

1. Read with the class-file parser under the intended recovery policy.
2. Modify code through stable instruction/label APIs: insert, remove, move, replace, branch retarget, add exception handler, or alter a switch.
3. Write with `ComputeFrames` and explicitly configured classpath/runtime sources.
4. Reparse with HippoCafe and inspect CP, Code and StackMapTable boundaries independently.
5. Load and exercise the emitted class using a matching Java runtime with verification enabled (e.g. `-Xverify:all`).
6. Compare expected behavior for valid semantic-preserving mutations; negative cases must reliably report failure.

JVM verification is a **critical external oracle**, but some methods are verified lazily and some invalid paths may not be executed. Include a Java test harness that loads, links/reflects where appropriate, and invokes the targeted methods rather than assuming the JVM verified every emitted method simply because a process launched. Test both class loading and behavior.

### G. Preservation and compatibility regressions

Run the existing full read/write regression corpus. In particular:

- Unchanged methods retain Code bytes, CP indices, raw attributes and original maxima/frames under `Preserve`.
- Ancient/Oak class files and intentionally malformed later-version attributes still parse/recover and write without mandatory analysis.
- A dirty modern Code with old frames cannot be emitted under `Preserve`, and `ComputeMaxs` cannot silently claim the frames are fixed.
- Dirty Code with unsupported opaque code-offset-bearing attributes fails until explicitly handled/dropped.
- Source bytes and diagnostics remain owned after caller input storage is freed.

### H. Fuzzing, sanitizers and resource limits

Fuzz archive entry enumeration, JMOD headers, multi-release manifest selection, lightweight hierarchy parsing, hierarchy graph creation, CFG construction and frame propagation. Reuse existing FuzzTest/libFuzzer conventions; maintain reproducible regression seeds. Record a minimized corpus for any crash or invalid write discovered. Enable ASan and UBSan in relevant CI configurations.

Test budgets for decompression, class hierarchy depth, graph edges, frame size, stack depth, worklist iteration and total allocation. Fuzz targets must not execute untrusted Java classes as part of the fuzz loop.

## Performance benchmarks

Benchmarks should report **time, peak RSS, allocations, and scaling**, not rely on unmeasured speedup claims:

| Scenario | What to measure |
|---|---|
| Register all supported JMODs | Indexing latency and memory without full bytecode decoding |
| Cold vs warm hierarchy lookup | Decompression count, time, cache hit rate |
| Large interface DAG | Transitive closure, pairwise assignability, join cost |
| Thousands of tiny methods | CFG/frame creation and per-method allocation overhead |
| Large branch/switch-heavy methods | CFG edges, frame worklist iterations, final layout convergence |
| Frame vs max-stack-only | Relative cost and agreement on computed maxima |
| Unmodified class round-trip | Preservation fast-path cost vs explicit analysis path |

Use bounded synthetic cases near format limits and actual JAR corpora. Benchmark in release builds with repeatable fixtures; investigate regressions before introducing a bespoke allocator or static hierarchy generator.

## Acceptance matrix

| Capability | Completion gate |
|---|---|
| JAR/JMOD/directory lookup | Correct explicit-source selection and bounded errors |
| Java runtime source | Loads from at least one modular development kit and one available legacy archive fixture |
| Multi-release JAR | Manifest- and target-release-correct definition selected |
| Hierarchy | Complete class/interface/array assignability with no silent missing-type fallback |
| CFG | Correct normal and exceptional successors, including pre-instruction handler states |
| Maxima | Correct u2-range maxima for supported valid methods; errors on invalid forms |
| Frames | Complete supported opcode simulation and verification-safe type merges |
| Writer | Computed Code metadata and final offsets validated; stale/unknown attributes handled safely |
| JVM validation | Representative generated classes are accepted by an independent target JVM |
| Compatibility | Existing historical/malformed-attribute read/write tests remain green |
| Robustness | Sanitizer/fuzz regressions pass; resource budgets enforced |

If a capability is not implemented, do not describe the analysis extension as universally verification-safe; report the supported profile and exact limitations.

## References

JVMS §4.7.3–§4.10 and §6; JAR File Specification (multi-release JARs); the project's existing parsing, writing, fuzzing and naming contracts in `01`–`09`.
