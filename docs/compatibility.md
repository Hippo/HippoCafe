# Compatibility and preservation

Parsing, preservation, semantic editing, and verification are distinct capabilities.
A successful compatible read does not certify that a JVM can load the class.

## Class-file profile

The codec supports standard constant tags and opcode forms, modified UTF-8, preview
minor versions, and historical short Code headers for versions 45.0 through 45.2.
Legacy `jsr`/`ret` instructions are supported for preservation through major 50.
The historical fixture name `OakTest.class` does not establish support for every
experimental Oak dialect; see the [fixture manifest](../tests/fixtures/README.md).

Frame analysis accepts majors 45 through 69 and applies version-aware verifier rules.
It reports unsupported subroutine analysis for `jsr`/`ret`. Unreachable modern code
that requires an inferable entry frame also returns a structured error. The reader's
preservation support is deliberately broader than the analysis profile. Future
versions are not a promise of support for unknown encodings or verification rules.

The implementation follows the [JVM class-file specification](https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-4.html)
and [instruction specification](https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-6.html).
The source format/opcode registries and fixture tests determine the supported profile.

## SSA profile

SSA lifting accepts decoded concrete methods in class-file majors 45 through 69.
The lowering semantics target Java 17 and later on a runtime that supports the
class version. This matters for older non-strict floating-point methods, where
historical JVMs could round an intermediate result when storing it to a local.
Legacy subroutines require return-address analysis and produce a structured error.

A current StackMapTable can supply the types for disconnected modern bytecode.
The lifter checks these frames against transfers within and out of the disconnected
region. A disconnected component without a usable entry state produces an error.
Ordinary class reading, bytecode editing, and preservation use their existing paths.

SSA lowering can change instruction layout, local slot assignments, and the
physical number of exception-table rows. Existing constant-pool operands retain their
indices. The lowerer rebuilds verification metadata and relocates known debug
tables and code type annotations when their destinations are representable.
Unmappable metadata fails lowering unless dropping is explicitly enabled.
A lowered method must fit the original class version's code, local, stack, and
exception-table limits. The model and emitted pools each have a 65,535-slot limit.

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

The default writer compacts the constant pool and remaps references in standard
attributes. It retains attribute order and preserves the selected bytecode offsets
during constant-pool remapping. Use `ConstantPoolWritePolicy::Preserve` with the
default Code options for byte-for-byte round trips of unchanged, clean classes.
Compatible recoveries may change output in either mode.

Editing Code requires freshly computed or explicitly supplied metadata unless the
writer can establish safe reuse. Maxima-only computation does not replace required
modern stack maps; reused stack maps can require preserving larger declared bounds.
Opaque Code emission requires both index preservation and the writer's
`allow_opaque_code` opt-in. Unsafe Code attributes are
rejected unless analysis-mode dropping was explicitly requested.

Unknown attributes require index preservation when emitted. Their bytes may refer
to original code offsets, which can become stale after edits.

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
