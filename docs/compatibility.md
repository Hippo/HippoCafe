# Compatibility and preservation

Parsing, preservation, semantic editing, and verification are distinct capabilities.
A successful compatible read does not certify that a JVM can load the class.

## Class-file profile

The codec supports standard constant tags and opcode forms, modified UTF-8, preview
minor versions, and historical short Code headers for versions 45.0 through 45.2.
Legacy `jsr`/`ret` instructions are supported for preservation through major 50.
The historical fixture name `OakTest.class` does not establish support for every
experimental Oak dialect; see the [fixture manifest](../tests/fixtures/README.md).

Analysis accepts majors 45 through 69 and applies version-aware verifier rules.
It reports unsupported subroutine analysis for `jsr`/`ret`. Unreachable modern code
that requires an inferable entry frame also returns a structured error. The reader's
preservation support is deliberately broader than the analysis profile. Future
versions are not a promise of support for unknown encodings or verification rules.

The implementation follows the [JVM class-file specification](https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-4.html)
and [instruction specification](https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-6.html).
The source format/opcode registries and fixture tests determine the supported profile.

## Attribute representation

| Representation | Attributes and behavior |
|---|---|
| Typed | Code, ConstantValue, Exceptions, SourceFile, Signature, BootstrapMethods, Synthetic, Deprecated |
| Recognized opaque | Annotations, StackMapTable, debug tables, InnerClasses/EnclosingMethod, method parameters, records, module metadata, nest metadata, permitted subclasses, and SourceDebugExtension |
| Unknown opaque | Unrecognized attribute bodies retained by their bounded envelope |

Recognized opaque attributes receive structural checks where supported, but their
payloads are retained as bytes rather than exposed as semantic editing APIs.
Computed emission can rebuild StackMapTable and relocate supported debug/type
annotation offsets without making those attributes public typed model alternatives.
Unknown private attributes are not semantically understood.

## Read policies

| Condition | Compatible (default) | Strict |
|---|---|---|
| Bounded malformed optional attribute | Drop and diagnose | Error |
| Known attribute illegal for version or location | Drop and diagnose without decoding an inapplicable body | Error |
| Trailing bytes after the class | Diagnose; serialization omits them | Error |
| Unknown constant tag or unsafe essential reference | Error | Error |
| Attribute exceeds its parent's bounds | Error | Error |
| Bounded, framed but undecodable Code | Error unless opaque-Code recovery is enabled | Error |

Diagnostics carry offsets, structural paths, and recovery actions. Their configured
limit bounds retained messages and reports suppression. Input, allocation, attribute,
annotation-depth, switch, method, and code-size limits are independent ceilings.

## Writer guarantees

Unchanged, clean classes retain constant-pool indices, attribute order, and original
Code bytes, including debug/verifier metadata and unusual permitted encodings.
Compatible recoveries intentionally change output. The library does not claim
byte-identical output for repaired, edited, or force-reencoded classes.

Editing Code requires freshly computed or explicitly supplied metadata unless the
writer can establish safe reuse. Maxima-only computation does not replace required
modern stack maps; reused stack maps can require preserving larger declared bounds.
Opaque Code emission needs a separate writer opt-in. Unsafe Code attributes are
rejected unless analysis-mode dropping was explicitly requested.

Unknown attribute bytes may contain private constant-pool or offset dependencies.
Retaining their bytes is not proof of semantic validity after arbitrary edits.

## Archive and resolution profile

JAR and JMOD editing supports Store and Deflate, ZIP64, explicit directory entries,
file-backed and owned-memory sources, and bounded staged edits. Retained entries
preserve compressed payloads, methods, timestamps, permissions, comments, and
supported timestamp/ownership/Unicode extra fields. Headers are regenerated;
byte-identical archive output and preservation of arbitrary unknown extra fields
are not promised. Encrypted, split, and unsupported compression/features are rejected.

Entry paths are case-sensitive, slash-separated, relative, and checked against
traversal and depth constraints. Editing a signed JAR requires deleting its
signature artifacts explicitly; HippoCafe does not sign archives or repair signatures.
JMOD output requires `classes/module-info.class` and entries under supported module
sections. Manifests and module metadata are supplied as ordinary entry bytes.

Classpath lookup honors manifest-enabled multi-release JARs for an explicit target
release. Registered directory roots may be symlinks; components below the root may
not be. Runtime discovery accepts supported JMOD or legacy runtime-JAR layouts,
not arbitrary linked runtime images. Resolution does not launch Java or download
runtime classes. It models one explicit classpath namespace, not custom loader
identity, module-layer visibility, or a complete JVM linker.

## Resource and failure boundaries

The parser, archive reader/editor, resolver, hierarchy engine, and analysis engine
have separate budgets. Configured exhaustion and cancellation produce structured
errors. General allocation failure may still throw. Do not mutate opened source
files or borrowed model data concurrently with an operation that reads them.

Malformed input must be checked through ordinary control flow. Assertions and
statically justified unchecked accesses are reserved for invariants established by
validated builders, bounded reads, or earlier validation stages.
