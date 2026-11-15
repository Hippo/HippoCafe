# 19 — Analysis Delivery Plan and Review Checklist

## Delivery philosophy

Implement the smallest complete, correct vertical slice first, then extend opcode and compatibility coverage while maintaining end-to-end tests. These are **dependency gates**, not time-based promises. A task is not finished merely because its public header compiles; it is finished when the affected fixtures, negative cases and integration checks pass.

## Gate 1 — Archive and resolver foundations

Deliver:

- Bounded ZIP/JAR/JMOD readers with central-directory indexing and entry-on-demand decompression.
- Manifest-aware multi-release JAR selection based on explicit target release.
- `ClassPath` construction, directory and in-memory providers, JDK archive discovery, provenance and deterministic precedence.
- Lightweight class-metadata scanning that reuses existing bounded class-file I/O rules.

Verify source precedence, no eager whole-class parsing, archive-size limits and errors for corrupt/missing entries. Keep no dependency on a spawned Java process or generated hierarchy header.

## Gate 2 — Class hierarchy

Deliver:

- Scoped type identities, `ClassInfo` caching, parent/interface traversal and cycle detection.
- Array-reference assignability and class/interface checks.
- Explicit missing-parent/error behavior; common reference joins with documented ambiguity handling.
- Epoch/snapshot discipline for in-memory modified classes and cached results.

Verify every hierarchy scenario in `12`/`18`, including shared interfaces, arrays and missing transitive dependencies. Do not begin verifier-safe frame output with an Object-only fallback implementation.

## Gate 3 — CFG and maxima

Deliver:

- CFG creation over stable instruction/label IDs; accurate normal and exceptional edge representation.
- Max locals from descriptors and local access width.
- Stack-shape/max-stack analysis with category-aware behavior and handler entry depth.
- Method/version/exception-table change detection for stale analysis.

Verify loops, switches, exception ranges and invalid stack shapes before integrating with the writer. A stack-depth-only proof must not be misrepresented as a type-state proof.

## Gate 4 — Full verification simulator

Deliver:

- Frame model, method-entry initialization, explicit category-one/two representation.
- Type transfer semantics for **every supported logical opcode**.
- Uninitialized allocations, constructor delegation and alias transitions.
- Hierarchy-backed reference assignment and merge, null/arrays/interfaces, local unusability, loop convergence, exception-handler pre-states.
- Actionable structured errors at instruction/block and hierarchy context.

Develop table-driven tests as opcode families are implemented. Avoid declaring the engine complete until its opcode audit has no unexplained gaps for the supported version profile.

## Gate 5 — StackMapTable and writer

Deliver:

- A `ComputeMaxs` and `ComputeFrames` write mode, preserving unchanged default behavior.
- A finalized layout/semantic emission plan that accounts for CP growth, branch relaxation, switch alignment and synthetic control-flow boundaries.
- Emission of valid full frames; optional compact-frame encodings may follow.
- Remapping of supported debug, exception and offset-bearing attributes, and rejection/explicit dropping of unsafe opaque attributes.
- Transactional output: no mutation of caller class on failure.

Verify `new` site offsets, the first implicit frame, all `offset_delta` rules, branch expansion and code-size limits against both independent wire checks and a JVM verifier.

## Gate 6 — Compatibility, fuzzing, performance and review

Deliver:

- Historical and malformed-attribute regression suite unchanged in behavior.
- Fuzz targets, sanitizer builds and resource-limit tests for archive/classpath and analysis inputs.
- Comparative benchmarks for JDK indexing, cold/warm resolution and method analysis.
- An independent code review covering correctness, API design, ownership, type merges and malformed-input boundaries; repair findings and rerun relevant validation.

Do not expand into SSA, whole-program optimization, JVM execution, full module/access checking, or automatic best-effort repair of unverified code just to avoid closing this gate.

## Review checklist — public interfaces

- [ ] Names match the existing Rust-style C++ conventions, and API results use owned values or documented resource lifetimes.
- [ ] No `ClassFile`/`MethodCode` dependency on `ClassPath` or `analysis` is introduced.
- [ ] In-memory classes and ordered archives are resolved through explicit scoped context; absent types are errors when needed.
- [ ] `ClassInfo` readers do not eagerly decode unrelated Code/attributes and remain resilient to irrelevant malformed optional attributes.
- [ ] Per-context cache identity and invalidation are correct; no raw pointers to archive temporary buffers escape.
- [ ] Error types distinguish not-found vs invalid, invalid vs unsupported, and unverified vs verified results.

## Review checklist — analysis

- [ ] Descriptor and opcode coverage are complete for supported class-file versions.
- [ ] Stack uses category widths; locals use actual JVM slots, including paired wide values.
- [ ] `dup*`/`pop*` forms are explicitly validated, not simulated by slot-count arithmetic alone.
- [ ] Handler state is taken **before** the throwing instruction, including constructor transitions.
- [ ] Hierarchy cycles and missing classes do not collapse to Object; ambiguous interface joins do not create unsound frames.
- [ ] `new`/`UninitializedThis` identities and aliases are tracked and never mistaken for initialized references.
- [ ] CFG/frame results detect edits to code, labels, switches and exception tables.
- [ ] The unsupported legacy subroutine path is explicit and preserves read/write compatibility.
- [ ] Unreachable regions receive principled treatment; no arbitrary type states or silent deletion.

## Review checklist — writer and resilience

- [ ] Original CP entries and original indices remain stable, even when frame types require new class constants.
- [ ] Conditional branch expansion, if supported, appears in the semantic/layout plan **before** verification states are finalized.
- [ ] StackMapTable uses finalized CP indices, byte offsets and `offset_delta` convention; uninitialized site offsets target real `new` instructions.
- [ ] The writer never reuses stale frames or code-offset-bearing opaque attributes after unsafe edits.
- [ ] Pre-stack-map class versions do not receive modern frames merely because analysis ran.
- [ ] `max_stack` and `max_locals` are checked against class-file limits; analysis never fabricates safe results under missing hierarchy.
- [ ] Malformed attributes cannot desynchronize archive metadata scanning or the class parser.
- [ ] Resource budgets and checked arithmetic cover hostile ZIP inputs, graph sizes, frame counts and worklist iterations.

## Definition of completion

The analysis extension is complete for its **declared supported version and opcode profile** when a caller can register appropriate JAR/JMOD/directory/in-memory class sources, resolve a hierarchy, mutate a method, compute maxima and frames where applicable, and write a verifiable class. Invalid or unsupported cases must fail with useful, bounded errors. The full historical parse/write corpus must still succeed without invoking analysis, and at least one independent JVM verification suite must validate transformed output.

The documents deliberately do **not** mandate a fixed amount of delegation, a particular ZIP library, or a benchmark speedup target. Those are implementation choices constrained by correctness, maintainability, and measured results.
