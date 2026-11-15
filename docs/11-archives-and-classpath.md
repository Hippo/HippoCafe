# 11 — Archive Readers, ClassPath, and Runtime Class Sources

## Objective

Provide deterministic, lazy access to class definitions from **registered in-memory classes, class directories, JARs, JMODs, and traditional JDK runtime archives**. This system supplies metadata to hierarchy resolution, not an eager object graph of every Java class. It must work as a native C++ library without launching Java or requiring a generated source-code tree.

## Source model

The `ClassPath` is an ordered collection of `ClassSource` providers. Sources should return a byte-owned or metadata-owned result, not a pointer into a temporary ZIP decompression buffer. Each result identifies its provenance (source type, path, archive entry, and selected multi-release version).

| Source | Lookup semantics | Notes |
|---|---|---|
| Registered in-memory class | Binary name to class metadata snapshot | Prefer edited/current class definitions; invalidate on metadata changes |
| Directory | Map internal name `p/C` to `p/C.class` under its root | Bounded file reads, normalized paths, symlink policy |
| JAR | ZIP central-directory index; root `.class` entries | Honor manifest-controlled multi-release entries |
| JMOD | JMOD wrapper plus ZIP; `classes/` entries | Ignore unrelated sections such as `lib/`, `bin/`, `conf/` |
| Legacy JDK archives | JAR sources found under an explicitly configured Java home | Handle `rt.jar` and appropriate ancillary archives when present |

Registration is explicit. Suggested API:

```cpp
ClassPathBuilder builder;
builder.set_target_release(target_release);
builder.add_class(current_class);              // in-memory metadata snapshot
builder.add_directory("build/classes");
builder.add_jar("libs/example.jar");
builder.add_jmod("jdk/jmods/java.base.jmod");
builder.add_java_home("/path/to/jdk");

auto class_path = builder.build();             // immutable source order/snapshot
ClassResolver resolver(std::move(class_path));
```

An alternative mutable builder is fine, but the final resolver must have a deterministic source list and cache epoch. An injected `ClassSource` abstraction is useful for applications that already have a class index; it must not require inheriting from `ClassFile`.

## Lookup and class identity

Use JVM **internal binary names** such as `java/lang/String` for ordinary class entries. Reject lookups containing path traversal, embedded NUL, absolute paths, malformed separators or other invalid archive paths; never concatenate an unchecked name into a filesystem path. Array descriptors (e.g. `[Ljava/lang/String;`) are synthetic types handled by the hierarchy, not `.class` archive entries.

The resolution order is explicit and stable:

1. Registered in-memory definitions (including those currently being transformed), subject to a defined duplicate policy.
2. Application/dependency source entries in the order registered by the caller.
3. Runtime sources registered through `add_java_home` or explicit runtime files.

Choose a default *first-definition-wins* policy for ordered external classpath entries, record duplicate shadows in optional diagnostics, and support a strict-duplicate mode for users who require unique definitions. Never silently replace the selected definition with a later archive entry. Do not pretend this reproduces JVM delegation, module-layer visibility, or multiple class loaders; the public documentation must call it a **classpath-style resolution context**.

Validate that the decoded class's declared internal name matches the name under which it was resolved. Report mismatch; do not cache it under a lying archive path. An absence is `NotFound`, not a fictitious `java/lang/Object` subtype. Root `java/lang/Object` must be obtainable or explicitly supplied in a self-contained hierarchy.

## Efficient JAR and JMOD indexing

- On registration, locate and validate the ZIP central directory once. Record entry metadata and physical compressed-data locations. Do **not** decompress every class.
- On lookup, select an entry, decompress that entry (if needed) into a bounded buffer, validate checksum and length, and extract only class metadata required for inheritance.
- Recognize ZIP `stored` and `deflate`; use a maintained ZIP library rather than implementing general ZIP decompression. Unsupported compression/encryption/ZIP features fail with a specific error. Support ZIP64 if the chosen library supports it safely; otherwise reject it explicitly.
- JMOD support recognizes its documented wrapper/header and class-entry prefix rather than treating it as a plain JAR at offset zero. Reuse the ZIP engine after validating/locating the embedded ZIP structure; do not skip an arbitrary number of bytes based on a guess.
- Never extract full archives to the filesystem merely to index their classes. Do not execute manifests, services, or arbitrary archive content.
- `module-info.class` and other non-hierarchy classes may be indexed or ignored for lookup by design, but their presence must not break source registration.

### Multi-release JARs

Respect the **JAR manifest**, not just paths matching `META-INF/versions/`:

1. Parse `META-INF/MANIFEST.MF` as a bounded, line-foldable manifest and recognize a main-section `Multi-Release: true` value per JAR rules.
2. If that value is absent/false, select only root class entries; never auto-select a version directory.
3. For a target Java release, select the highest applicable versioned definition for that class, with the root entry as fallback. Ignore nonconforming or disallowed version folder names; enforce the JAR spec's minimum versioned-directory release.
4. Validate the selected entry's class-file version against target compatibility where applicable; report impossible combinations as a conflict or unsupported format rather than silently changing the target.
5. Cache selection by `(source identity, internal name, target release)`. Switching target release creates a different resolver snapshot.

Manifest parsing is only for archive selection. It is not general JAR manifest editing, signing verification, or Java module resolution.

## Java home discovery

`add_java_home(path)` should inspect the provided installation without assuming the host's `JAVA_HOME` is correct:

- Prefer available `jmods/*.jmod` for full development kits using modular packaging.
- Support legacy layouts with an `rt.jar` runtime and relevant additional runtime archives when present.
- Diagnose an installation that only has a linked runtime image (`lib/modules`) and no supported class archives. A future `jimage`/`jrt` source can support these images independently; do not claim JMODs exist in every runtime.
- If a Java home contains several possible sources, follow a documented deterministic order and avoid ambiguous duplicate core classes.
- No Java executable invocation or online download is required for resolution.

JMODs and JARs are **inputs** to the resolver; they are not necessarily runnable application artifacts. Runtime discovery should produce useful provenance in errors, e.g. “`java/lang/Object` missing from configured Java home” rather than silently using host classes.

## Metadata-only class scan

A class resolver requires primarily:

```cpp
struct ClassInfo {
    ClassName name;
    std::optional<ClassName> super_name;
    std::vector<ClassName> interfaces;
    uint16_t access_flags;
    ClassVersion version;
    SourceId source;
};
```

Only a lightweight bounded scanner is needed to locate class name, superclass and interfaces from the header and constant pool. Reuse the existing checked binary reader and modified-UTF-8 decoding rules; do not instantiate `MethodCode`, allocate per-instruction slots or decode method attributes to answer inheritance queries. The metadata scanner must still validate CP tags and bounds used to resolve its fields. A malformed header or inconsistent CP reference is a resolution error. A well-bounded irrelevant optional attribute must not cause a hierarchy lookup to fail solely because its payload is malformed.

Paths and names in diagnostics are owned strings or stable IDs; no dangling `string_view` into temporary buffers.

## Resource limits and archive security

Define configurable limits for archive count, central-directory entries, per-entry uncompressed size, total decompression per resolution, compression ratio, path/name length, metadata cache bytes, file reads, and recursion depth. Use checked arithmetic for offsets and sizes. Validate CRC and declared length; distinguish `ArchiveCorrupt`, `UnsupportedCompression`, `EntryTooLarge`, `MalformedClassMetadata`, `ClassNameMismatch` and `NotFound`.

- Never follow `../`, absolute, device, or symlink escape paths for directory sources; choose and document whether in-tree symlinks are supported.
- ZIP symlinks, duplicate ZIP names and overlapping records require an explicit deterministic/reject policy. Do not select an attacker-chosen duplicate entry nondeterministically.
- Do not let a ZIP bomb, malicious count, or circular hierarchy trigger unbounded allocation or infinite recursion.
- Support cancellation/resource budgets for work on untrusted archive corpora.

## Acceptance examples

- Register an application JAR and `java.base.jmod`; resolve application class, `java/lang/Object` and `java/util/List` without eagerly decoding unrelated methods.
- Register an older runtime archive and resolve the same name using that source, not the host JDK.
- A declared in-memory superclass overrides a shadowed external class under the configured precedence; cache is invalidated when the snapshot changes.
- Multi-release JAR containing root, versioned classes, and manifest: appropriate definitions selected for two target releases; no version-selection if manifest is absent.
- Empty/missing/corrupt/oversized/encrypted JAR and JMOD inputs produce bounded structured errors.
- An archive entry named `p/C.class` whose declared class is `p/D` never poisons a cached hierarchy entry for `p/C`.
