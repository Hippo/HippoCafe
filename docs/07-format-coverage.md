# 07 — Format Coverage and Version-Aware Features

## What 'support' means in pass one

An item can have three support levels:

- **Typed:** parsed into a semantic structure with specific fields and emitted through a matching codec.
- **Opaque:** framing is understood (`attribute_name_index`, `attribute_length`, bytes), but the payload is preserved verbatim without interpreting its contents. Useful for faithful read/write, not for semantic editing.
- **Rejected / recovered:** an unknown CP encoding or unframed/truncated core structure causes a structured error; a safely bounded and inapplicable/malformed nonessential attribute may be dropped with a diagnostic.

If an attribute is valid and supported by the declared class version but not implemented as typed yet, **Opaque is a supported pass-one round-trip path**, provided it remains unmodified and its CP/offset dependencies are stable. Do not mark such an attribute as 'parsed' in the sense of semantic introspection.

The reference specification is the JVM Specification Chapter 4 (class-file format) and Chapter 6 (instruction formats). Names, locations, version-introduction rules, and applicable constraints belong in a version-aware registry. Do not hard-code a single current class-file major version as the only accepted format.

## Constant pool coverage: typed, mandatory

| Tag | Meaning | Parse/write requirement |
|---|---|---|
| 1 | `CONSTANT_Utf8` | Preserve raw Java modified-UTF-8 bytes; decode lazily |
| 3, 4 | `Integer`, `Float` | Preserve exact 32-bit bits |
| 5, 6 | `Long`, `Double` | Preserve exact 64-bit bits; consume second CP slot |
| 7, 8 | `Class`, `String` | Preserve referenced `CpIndex` |
| 9, 10, 11 | Field/Method/InterfaceMethod references | Preserve typed referenced indices |
| 12 | `NameAndType` | Preserve name and descriptor indices |
| 15 | `MethodHandle` | Reference kind and reference index; version-aware |
| 16 | `MethodType` | Descriptor index |
| 17 | `Dynamic` | Bootstrap method index + NameAndType |
| 18 | `InvokeDynamic` | Bootstrap method index + NameAndType |
| 19, 20 | `Module`, `Package` | Name index; version-aware |

No unknown-tag skip unless an explicitly registered, version-specific byte length is known. Unknown CP tags by themselves are not length-delimited, so guessing is unacceptable. Validate standard tag fields in Strict mode; in Compatible mode preserve known-tag raw structure when a newer tag appears in an older declared version, with diagnostics and controlled writer restrictions.

## Standard attributes, by context

The table lists **minimum** typed support for pass one and the fallback for other recognized formats. The implementation may type more attributes, but cannot silently discard a valid supported attribute merely because semantic support has not yet been implemented.

| Context | Attribute / family | First-pass disposition |
|---|---|---|
| Class | `SourceFile`, `Signature` | **Typed** |
| Class, field, method | `Synthetic`, `Deprecated` | **Typed** zero-length marker |
| Field | `ConstantValue` | **Typed** |
| Method | `Code`, `Exceptions` | **Typed** (`Code` may have opaque essential fallback for unsupported bytecode) |
| Class | `BootstrapMethods` | **Typed** to support dynamic constants/indy linkage |
| Code | `LineNumberTable`, `LocalVariableTable`, `LocalVariableTypeTable` | **Typed preferred; opaque accepted** if pass-one offset preservation is respected |
| Code | `StackMapTable` | **Opaque preservation required**; structural decode optional, no computation |
| Class | `InnerClasses`, `EnclosingMethod` | **Opaque minimum**; typed recommended |
| Class | `NestHost`, `NestMembers`, `PermittedSubclasses` | **Opaque minimum**; typed recommended |
| Class | `Record` (including component attributes) | **Opaque minimum**; typed recommended |
| Class | `Module`, `ModulePackages`, `ModuleMainClass` | **Opaque minimum**; typed recommended |
| Class | `SourceDebugExtension` | **Opaque preservation** (arbitrary byte content) |
| Class/field/method/record component | Runtime visible/invisible declaration annotations | **Opaque minimum**; typed optional |
| Method | Runtime visible/invisible parameter annotations, `AnnotationDefault`, `MethodParameters` | **Opaque minimum**; typed optional |
| Class/field/method/code/record component | Runtime visible/invisible type annotations | **Opaque minimum**; typed optional |
| Any allowed attribute-bearing context | Unknown vendor/custom attribute | **Opaque preservation** if name and length are valid |

For context entries referring to Record components, the whole `Record` attribute may be opaque in pass one. If Record is typed later, recurse into its component attribute lists with the same recovery policy. Other standard attributes that appear in newer JVM specifications should be added to the registry as appropriate; if unknown to this version of the library, preserve as a vendor-like opaque attribute rather than assuming corruption.

### Version gating is different from 'unknown'

Two superficially similar attributes have materially different handling:

1. `CustomDebugInfo` is not recognized: under the JVM extensibility model, preserve it as an opaque, bounded, noncritical attribute.
2. `RuntimeVisibleAnnotations` on a class whose declared version predates that standard attribute: treat it as **recognized but inapplicable**, do not decode the modern grammar, and drop it by default in Compatible mode with a diagnostic.

A newer standard attribute is not a generic vendor extension just because an older class claims to contain it. This distinction prevents version-skew obfuscation from defeating the parser.

## Class header, fields and methods

Support the full class-file envelope:

```text
magic u4
minor u2, major u2
constant_pool_count u2, entries...
access_flags u2
this_class u2, super_class u2
interfaces_count u2, interfaces[]
fields_count u2, field_info[]
methods_count u2, method_info[]
attributes_count u2, attribute_info[]
```

Validate counts and cross-references in a version-aware fashion. Preserve modifier/access bits in Compatible mode. Do not assume that every valid class file has `super_class != 0` or that every concrete method's Code is automatically valid. Method and field names are CP Utf8 indexes, not independently allocated string copies.

## Bytecode instruction coverage

The decoder/encoder must handle all standardized opcode **forms** encountered by supported class-file versions, including:

- No-operand, immediate, implicit-index, explicit-index, CP-operand, branch and return instructions.
- All `wide` cases (loads, stores, `ret`, `iinc`).
- `ldc`/`ldc_w`/`ldc2_w` with type-appropriate legality.
- `goto`/`goto_w`, `jsr`/`jsr_w`, conditional 16-bit branches.
- `tableswitch`, `lookupswitch` including signed keys and alignment padding.
- `invokeinterface` count+reserved byte and `invokedynamic` reserved zero words.
- `newarray` atype, `multianewarray` dimensions.
- Code exception handler table and legal handler/end offsets.

Treat reserved opcode bytes or malformed `wide` sequences as unsupported/malformed, not as spare valid instructions. Compatible mode may retain the whole Code attribute as opaque if fully bounded; Strict mode fails. Parser and writer must agree on a single opcode metadata table, preventing inconsistencies between offset scan and decode.

## Historical support profile

Historical compatibility is a first-class testing dimension, not a speculative feature flag:

- **Baseline:** standard class-file version 45.x, including minor-version variants accepted by historical JVMs, and subsequent major versions covered by implemented CP and opcode tags.
- **Early experimental files:** pre-release Oak/HotJava-related class files may differ in binary details even when the major version resembles standardized Java. Add a dedicated fixture-backed compatibility adapter only when needed. If decoding is ambiguous, report `UnsupportedDialect` or preserve bounded opaque contents where possible; never misparse and declare success.
- **Modern:** `Dynamic`/`InvokeDynamic`, bootstrap metadata, modules, records, nestmates, sealed-class metadata, annotations, stack-map data and future unknown attributes must all survive clean, unmodified read/write through typed or opaque representations where the original envelope is well formed.
- **Preview / minor versions:** preserve minor exactly; validation follows the applicable specification and configured strictness rather than forcing minor to zero.

## No analysis functionality

Not implemented here: rebuilding stack-map frames, inferring max stack or locals, verifier type checking, CFG, SSA, classpath hierarchy, dead-code reachability or compiler-level translation. Original verifier metadata is data to preserve, not an invitation to calculate it.
