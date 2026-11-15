# 17 — Historical Compatibility, Recovery, and Resource Safety

## Goal

Keep the class-file reader's deliberately tolerant compatibility semantics while ensuring **analysis is sound where it claims to be sound**. Parsing a historic or obfuscated class and generating fresh verifier metadata are separate operations. The former may succeed in compatible mode when the latter must report unsupported, incomplete, or invalid semantics.

## Version-aware behavior

| Class-file family | Reader/writer | Analysis expectations |
|---|---|---|
| Early Oak / legacy Java class files | Preserve existing compatibility and round-trip support | Analyze supported opcodes/descriptors; no modern frame insertion; unsupported historical semantics explicit |
| Pre-stack-map verifier versions | Decode and preserve, including old attributes | Maxima and optional legacy inference where supported; no modern StackMapTable generation |
| Version 50 | Preserve frames when unchanged | Modern frame computation when requested; do not rely on optional fallback verification by inference |
| Version 51+ | Version-aware parsing and writing | Modern frame type checking and output where applicable; `jsr`/`ret` restrictions enforced |
| Newer/future versions | Preservation-first when grammar is known | Reject unsupported new verification rules/features rather than claiming universal analyzability |

The precise version gate for each opcode and attribute is defined by the applicable JVM specification and the class file's declared major/minor version, not the OS, compiler, or whichever Java installation happens to be available.

## Legacy subroutine instructions

Old `jsr`/`jsr_w`/`ret` encode subroutine call/return-address semantics. They may appear in inputs the parser intentionally supports. There are three acceptable outcomes:

1. The method is untouched and the writer safely preserves original Code and metadata.
2. A separately implemented, tested legacy analysis understands return addresses and subroutine control flow sufficiently to compute its requested maxima.
3. The analyzer returns `UnsupportedSubroutineAnalysis`, leaving the canonical parsed method untouched.

Do not flatten or inline subroutines implicitly. Do not emit modern StackMapTable `ReturnAddress` entries. Do not treat `ret` as `return`, a straight fallthrough, or a generic local load.

## Poisoned attributes and version mismatch

Obfuscated inputs may intentionally attach attributes introduced in later Java versions to older class files, with malformed or misleading payloads. The existing reader policy remains authoritative:

- Parse each attribute in a strictly bounded subreader according to its declared length.
- Apply version-aware typed parsing only where legal; skip/drop an invalid nonessential attribute in compatible mode with a diagnostic.
- Do not let a malformed later-version attribute desynchronize subsequent attributes, members or class structure.
- Preserve well-bounded unknown attributes when safe, but never interpret their bytes as trusted structural data for hierarchy or frame computation.
- Do not drop an essential, malformed `Code` payload and then claim the method has been analyzed; preserve opaque Code only under explicit compatible policies, with analysis unsupported.

Hierarchy metadata extraction must be tolerant of **irrelevant optional attributes** while being strict about the actual fields needed for inheritance. This prevents a poisoned annotation attribute from blocking class hierarchy lookup if the header and CP fields are valid. However, malformed superclasses, descriptors, CP tags or method bodies needed for verification are real analysis failures; no synthetic repair.

## Untrusted archives and resource budgets

Archive and graph analysis inputs can be adversarial. Define bounded defaults and caller-configurable limits for:

- Archive file size, number of entries, entry name length, compression ratio, total and per-entry decompression.
- Per-class metadata bytes, path depth, class count, descriptor nesting/array dimension checks.
- Hierarchy recursion depth, discovered ancestors/interfaces and pair-cache sizes.
- Code length and instruction count (subject to JVM format), handler count, CFG nodes/edges.
- Maximum stack/local slots, frame count, frame-entry storage, worklist iterations and aggregate analysis memory.

Validate `count * element_size`, offsets, ZIP directory records, and decompressed lengths with checked arithmetic. Never allocate a claimed length without validating it against input bytes and budgets. Exceeding a budget is a structured `ResourceLimitExceeded` or cancellation result, not a crash or infinite loop.

Fuzzers should target (a) ZIP/JMOD/manifest indexing, (b) metadata-only class lookup, (c) handler/CFG construction from malformed-but-decodable methods, and (d) frame propagation with adversarial descriptor/CP content. Use sanitizers and avoid trusting the validity of `MethodCode` simply because it came from an API caller rather than parsed bytes.

## Missing classes, ambiguous types, and fallback policy

By default a verification-safe analyzer must **fail closed**:

- Missing class or transitive parent -> `ResolutionError`, not Object.
- Ambiguous or unsatisfied frame join -> explicit diagnostic, not arbitrary interface selection.
- Unsupported new opcode/verification feature -> explicit unsupported status, not zero stack delta.
- Class hierarchy cycle -> explicit error, not an infinite search or a guessed superclass.
- Broken bytecode -> explicit `InvalidCode`, even if raw bytes can still be preserved.

A future best-effort exploration mode may report partial results (e.g. stack height on incomplete classpath), but must be labeled non-verification-safe and must never feed `ComputeFrames` without verification and complete required dependencies.

## Class-loader and module boundaries

A classpath resolver simulates a **chosen deterministic namespace**, not the complete JVM loader identity model. A JVM type is properly identified by name and loader; modules also affect resolution and access. The core API may reserve namespace identity for future resolver plugins, but should not claim full runtime fidelity across custom loaders, patched modules or runtime images not explicitly indexed.

For standard applications with explicit classpath/JDK inputs, this simplification is useful and testable. Where two possible definitions would alter frame correctness, require the user to select the resolution context or reject conflicting inputs in strict mode.

## Security and reliability tests

- Ancient/Oak or otherwise legacy fixtures remain parseable/writeable without new analysis invocation.
- A too-new malformed attribute embedded in an old class is discarded with a precise diagnostic and does not break subsequent metadata extraction.
- Corrupted/oversized compressed entries, central-directory tricks, duplicate names, ZIP path traversal and invalid JMOD wrapper fail safely.
- Malformed super chains, missing interface, massive interface diamonds, cyclic metadata and invalid names have bounded execution.
- Analyzing a malformed method never writes invalid metadata into its owner or corrupts another method.
- Emitting a dirty Code with unknown offset-bearing attributes fails unless the user explicitly drops those attributes.

## Compatibility contract

**Read support must not be reduced to the subset analyzable by the frame engine.** The library should continue to function as a resilient bytecode inspection/round-trip tool, with `AnalysisError` separating optional computation failures from `ReadError` and `WriteError`. This distinction is a permanent architectural rule, not a temporary migration step.
