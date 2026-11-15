# 10 — Analysis Architecture and Contracts

## Purpose

Extend the class-file library with class resolution and the JVM code analyses required to produce valid `Code.max_stack`, `Code.max_locals`, and `StackMapTable` metadata after bytecode edits. The architecture must remain usable without executing Java code, without a pre-generated runtime hierarchy, and without compromising the resilience and round-trip behavior of the reader/writer.

This document extends the ownership, class model, bytecode, and writer contracts in `01`–`09`. The existing class representation remains canonical: `ClassFile` owns methods and constant-pool entries; `MethodCode` owns compact instructions and stable labels; class-file writing continues to preserve unchanged bytecode and opaque attributes. The new facilities are **services over that model**, not a second class model.

## Required capabilities

1. Read class definitions from explicit in-memory classes, class directories, JARs, and JMODs; discover traditional JDK class archives when requested.
2. Resolve superclass/interface relationships and JVM reference-type assignability using a configurable, reproducible resolution context.
3. Build a control-flow graph for a decoded `MethodCode`, including branch, switch, and exception-control-flow edges.
4. Simulate JVM verification types through instructions to obtain stable entry states at required stack-map boundaries.
5. Compute the required maximum local slots and operand-stack slots, as separate public operations and as part of frame generation.
6. Integrate calculated metadata into safe class-file writing without preserving stale offsets or corrupting unknown attributes.
7. Report incomplete hierarchies, unrepresentable merges, malformed bytecode, unsupported legacy semantics, and invalid frames as structured errors, never as invented 'success'.

No SSA, dominators, call-graph construction, deobfuscation heuristics, JVM interpreter, linker, or full module/class-loader implementation is required. Standard opcode semantics used by the frame engine **are** required; treating unknown semantics as a generic stack delta is not acceptable.

## Dependency boundaries

```text
                 Application / caller
                         |
              +----------+----------+
              |                     |
       AnalysisContext            ClassWriter
              |                 (analysis mode)
        +-----+------+              |
        |            |              |
    ClassHierarchy  FrameAnalyzer --+
        |            |
    ClassResolver   ControlFlowGraph
        |            |
      ClassPath      MethodCode / labels
        |
  Source adapters (memory, directory, JAR, JMOD)
        |
      Bounded binary/ZIP utilities

       ClassFile / ConstantPool / MethodCode
                 (canonical model)
```

- `cafe::classpath`: source registration, indexing, metadata retrieval, hierarchy/cache policy. It may use bounded class-header parsing helpers from `io`, but `ClassFile`, `MethodCode`, and the general reader must never depend on `classpath` or `analysis`.
- `cafe::archive`: safe ZIP/JAR/JMOD ingestion and indexed entry access; no JVM semantics beyond archive selection.
- `cafe::analysis`: CFG, verification types, simulator, merge, maxima, and frame materialization.
- `cafe::io`: owns final byte layout, CP allocation, StackMapTable encoding, and the write transaction. It calls analyses through a narrow integration boundary; analysis never writes a class file.
- `src/detail/`: internal allocators, checked arithmetic, opcode helpers, source indexes. Do not install internal headers.

Prefer one exported `hippocafe::hippocafe` target. Source-folder boundaries are not grounds for empty packages or speculative plugin layers. Archive dependencies must be linked privately where feasible.

## Naming and source layout

Continue the documented Rust-inspired C++ naming: types and enum members `PascalCase`, functions/variables/files/namespaces `snake_case`, compile-time constants `SCREAMING_SNAKE_CASE`, C++23, `std::expected`, `std::pmr` where ownership warrants it. All public types remain under `cafe` or its clear subnamespaces. No Java-style camelCase, no class-inheritance interface convention, no `m_` prefixes.

Add only implemented files, using this responsibility map:

```text
include/cafe/
  archive/
    jar.hpp                  # if standalone archive API is made public
    jmod.hpp
  classpath/
    class_path.hpp
    class_resolver.hpp
    class_hierarchy.hpp
    class_info.hpp
  analysis/
    context.hpp
    control_flow_graph.hpp
    verification_type.hpp
    frame.hpp
    frame_analyzer.hpp
    max_stack.hpp
    max_locals.hpp

src/
  archive/
    zip_archive.cpp
    jar.cpp
    jmod.cpp
  classpath/
    class_path.cpp
    java_home.cpp
    class_metadata_reader.cpp
    class_resolver.cpp
    class_hierarchy.cpp
  analysis/
    control_flow_graph.cpp
    verification_type.cpp
    frame_analyzer.cpp
    frame_merge.cpp
    instruction_effects.cpp
    max_stack.cpp
    max_locals.cpp
  io/
    stack_map_encoder.cpp
    code_analysis_write.cpp

tests/
  unit/                      # joins, opcodes, hierarchy, max slots
  integration/               # archive resolution, verifier acceptance
  regression/                # historical and adversarial class files
  fixtures/                  # source + precompiled artifacts
fuzz/                        # archives, classpath metadata, CFG/frame inputs
benchmarks/                  # lookup, analysis, peak memory, writing
```

Names are a suggested arrangement, not a demand to create placeholders. Place `CodeLayout` and `StackMapTable` encoding beside the existing writer logic; do not duplicate them in analysis.

## API shape and error reporting

The following signatures illustrate the intended contract rather than requiring literal members or spellings:

```cpp
namespace cafe {

class ClassPath;
class ClassResolver;
class ClassHierarchy;
class AnalysisContext;
struct ControlFlowGraph;
struct FrameAnalysis;
struct AnalysisError;

std::expected<ControlFlowGraph, AnalysisError> build_cfg(
    const MethodCode& code,
    std::span<const ExceptionHandler> handlers,
    std::pmr::memory_resource* resource
);

std::expected<uint16_t, AnalysisError> compute_max_locals(
    const Method& method,
    const ClassFile& owner
);

std::expected<uint16_t, AnalysisError> compute_max_stack(
    const ClassFile& owner,
    const Method& method
);

std::expected<FrameAnalysis, AnalysisError> compute_frames(
    const ClassFile& owner,
    const Method& method,
    AnalysisContext& context
);
}
```

These must either return self-owning results or explicitly require a caller-owned resource/context whose lifetime exceeds the result. Never return PMR-backed data referencing a destroyed local scratch arena. Do not expose `src/detail` handles as public types. Public APIs should accept stable `InstructionId` and `LabelId`, not transient slot pointers.

An error identifies at least the affected class and method (if applicable), problem kind, and an instruction/label or byte offset when known. Resolution failures also include the requested binary name, search context, and source/cause. `std::expected` errors are distinct from warnings; a missing ancestor that affects frame correctness must not be downgraded to a warning.

## Versioning, invalidation, and ownership

- `MethodCode` already has a mutation version. Every CFG or frame result records that version. A subsequent edit invalidates the result; accessors/writer must detect stale analysis.
- Hierarchy queries operate within a well-defined **resolution snapshot**: source order, target Java release, class definitions, and identity space. Cache entries belong to that snapshot. If a registered class changes its superclass or interfaces, rebuild the snapshot or perform explicit epoch invalidation before further analysis.
- Metadata records from external classpath entries are owned by the resolver context, not by arbitrary temporary buffers. Archive handles stay alive until all referenced metadata has been fully copied/decoded.
- Different classes and method analyses may execute on separate threads. Do not share an `unsynchronized_pool_resource` across concurrently mutating contexts. Immutable source indexes may be shared if cache synchronization is correct.
- Type identities are scoped to one resolver context; a numeric type ID must not silently mean a different class in another context. An optional namespace/loader identity should be reserved for a future provider but never falsely claim full JVM class-loader semantics.

## Safety and compatibility invariants

- A class that can be parsed/round-tripped is **not** necessarily analyzable. Preserve parsing and writing of unusual/legacy classes independently of analyzer support.
- Unresolved types, hierarchy cycles, ambiguous or unverifiable frame joins, malformed descriptors, invalid instructions, and unsupported subroutines yield explicit errors in verification-safe modes.
- Computation must not mutate a caller's `ClassFile` merely to inspect it. The writer may construct a temporary, fully specified emission plan.
- A classpath must not be inferred from the current host JDK without an explicit user choice; the target runtime can differ from the build/runtime host.
- An analysis mode may calculate correct maxima yet still be unable to issue valid modern frames. The two success conditions are different.
- All externally supplied paths, ZIP archives, metadata and instructions are untrusted; parsing and worklist algorithms have checked bounds and configurable resource limits.

## Authoritative specifications

Implement JVM legality from the [Java Virtual Machine Specification](https://docs.oracle.com/javase/specs/jvms/se25/html/index.html), especially §4.7.3, §4.7.4, §4.9–§4.10, and §6. The exact version linked is a reference edition, not a target runtime commitment; apply the matching specification for each supported class-file version. The [JAR File Specification](https://docs.oracle.com/en/java/javase/25/docs/specs/jar/jar.html) governs multi-release archive selection. JVM implementations are conformance oracles, not substitutes for explicit algorithmic tests.
