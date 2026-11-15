# 05 — ClassReader, Compatibility, and Malformed-Attribute Recovery

## Purpose

The reader is a **safe structural parser** designed for ordinary JVM class files, historical class files (including tested early 45.x variants), and hostile/obfuscated input. A file with a malformed annotation, unsupported future attribute, or invalid debug attribute should not necessarily make the whole class uninspectable. Conversely, recovery must never guess unknown data lengths, read past declared boundaries, desynchronize the constant pool, or manufacture a semantically valid class.

## Reader policies

```cpp
enum class ParseMode { Strict, Compatible };

struct ReadLimits {
    size_t max_input_bytes;
    size_t max_total_allocated_bytes;
    size_t max_attributes;
    size_t max_annotation_depth;
    size_t max_switch_cases;
    size_t max_methods;
    // Include a cap on total warning count.
};

struct ReadOptions {
    ParseMode mode = ParseMode::Compatible;
    ReadLimits limits = ReadLimits::defaults();
    // Additional explicit options for legacy dialects and raw recovery.
};
```

Practical starting limits (all configurable): 64 MiB input, 256 MiB total decoded allocation budget, 100,000 total attribute envelopes, 128 levels of nested annotation content, 65,535 method entries (wire limit), 65,535 bytes of method code (format limit), and at most 1,024 recorded diagnostics before an aggregation warning. Derive the maximum switch case count from remaining Code bytes as well as an explicit cap. Do not allocate from a `u4` length before checking the enclosing span and total budget.

`Compatible` is the default for a manipulation tool; `Strict` is offered for validators/tests. Both enforce memory safety, structural framing, and resource limits. A compatible parse with diagnostics is **not equivalent** to JVM verification or a guarantee of a loadable class file.

| Condition | Strict | Compatible default |
|---|---|---|
| Valid known attribute applicable to version/context | Parse according to supported codec | Same |
| Unknown custom attribute with valid name and bounded body | Preserve opaque | Preserve opaque |
| Known attribute appears in version older than its introduction | Version/format error if disallowed by policy | **Do not invoke its codec; drop and diagnose** |
| Known attribute appears in an invalid location | Format error | Drop and diagnose if nonessential and bounded |
| Known nonessential attribute payload malformed but length valid | Error | Drop entire attribute, emit diagnostic, resume at boundary |
| Unknown attribute name index invalid, envelope bounded | Error | Drop and diagnose (cannot represent a valid name) |
| Bad attribute length running past remaining enclosing input | Error | **Error**; no safe next boundary |
| Unknown CP tag without defined entry length | Error | **Error**; cannot resynchronize |
| Bad magic / truncated constant pool / truncated method table | Error | **Error** |
| Malformed Code but bounded `attribute_length` | Error | Preserve as explicit opaque essential Code if enabled; otherwise structured error |
| Bad bytecode branch target / invalid opcode in Code | Error | Optional `OpaqueCode` recovery with diagnostic; never fake instructions |

On old class files containing newer named attributes, **version-gating before decoding** is crucial. Some obfuscators deliberately insert modern attribute names with invalid modern-format bodies into older class-file versions. The parser should be able to skip these safely by their declared length, without attempting to parse their contents. For this reason the default compatibility policy drops recognized-but-version-inapplicable attributes (rather than preserving actively misleading data). An advanced raw-preservation option can be added, but it must be opt-in and clearly labeled.

## Exact attribute parsing algorithm

Each attribute occurs in an enclosing context with a known boundary. Read its `u2 name_index`, `u4 length`, then validate `length <= reader.remaining()` with checked subtraction (not `cursor + length` without overflow checks). Construct an **independent bounded sub-reader** for exactly those bytes. The outer cursor must not be moved speculatively by an inner decoder.

Pseudocode:

```text
read_attribute(parent, version):
  name_index = outer.read_u2()       OR fatal
  length     = outer.read_u4()       OR fatal
  body       = outer.take_slice(length) OR fatal  # advances exactly length
  name       = cp.try_utf8(name_index)
  if name invalid:
      return drop_with_diagnostic("invalid attribute name") if Compatible
      otherwise error
  spec = lookup_attribute(name)
  if spec is known and not applicable_to(version, parent):
      return drop_with_diagnostic("inapplicable attribute") if Compatible
      otherwise error
  if spec is unknown:
      return RawAttribute(name_index, body) # zero copy slice to class-owned bytes
  temp = decode_known_in_bounded_reader(spec, body)
  if decode failed OR temp.reader.remaining() != 0:
      if nonessential and Compatible:
          return drop_with_diagnostic("malformed attribute")
      return error_or_explicit_opaque_essential()
  commit(temp)                        # atomic; no partial parent mutation
```

An attribute decoder may itself parse nested attributes (Code, Record). It must use the same bounded algorithm recursively and can drop individual malformed nested nonessential attributes without losing its entire parent if the parent's structure remains intact. Context and version applicability must be checked at every nesting level.

**No silent cursor reset:** the outer reader has consumed exactly the declared slice, regardless of success/failure of internal decoding. Parsing then proceeds to the next sibling. **No partial mutation:** a failing decoder must not leave half-built annotations, unfinished exception tables, interning changes or inserted instructions in the class. Decode to temporary scope then commit, or implement an explicit rollback mechanism.

## Binary reader invariants

- Big-endian `u1/u2/u4/u8`, signed variants via bit-preserving conversion; do not cast unaligned input bytes to C++ structs.
- Operations are bounds-checked; `remaining()` is authoritative; `take_slice(size)` checks before advancing.
- Offsets and lengths use `size_t` or checked 64-bit arithmetic; never wrap `u4` into a narrower offset.
- `u2` counts/indices and `u4` lengths are validated before `reserve`/iteration. A count of 65535 does not imply the bytes for 65535 entries are present.
- MUTF-8: store original bytes; decode lazily using correct Java modified UTF-8 rules, with strict validation for required symbolic names and compatibility diagnostics when recoverable. Do not treat CP Utf8 as normal UTF-8.
- Float and double values are stored as raw bits; no conversion that canonicalizes NaNs.
- CP `Long`/`Double` occupy two slots. Validate last-slot constraints and CP references. Unknown tags are fatal without a proven version-specific codec.
- `ClassFile` holds an owned input backing; raw slices cannot outlive it. Offset-based source slices are checked once at construction and again before write if source state changed.
- Enforce end-of-input checking: unexpected trailing class bytes are strict errors; compatibility mode may report and retain/discard trailing bytes only under an explicit documented policy, not silently.

## Parsing stages (no forward-reference traps)

1. **Header**: magic, minor/major, CP count. Preserve version exactly. Decide compatibility dialect using explicit version rules and fixtures, not automatic upgrade.
2. **Constant pool scan**: decode slot/tag structure and preserve original indices. Do not resolve CP references eagerly if forward references may exist; validate them in a later pass.
3. **Class/member envelopes**: read class fields, methods and attribute headers using bounds-checked loops. Do not parse arbitrary nested attr bodies before CP is available.
4. **Attribute decode**: apply version/location registry and transactional bounded sub-readers. Code decoding may require a mapping from original bytecode offset to label.
5. **Code decode**: verify each instruction's encoded length, `wide` operands, switch padding/counts, branch target alignment/range. Bind labels and exception boundaries. Preserve original code bytes.
6. **Cross-reference checks**: typed CP references, relevant bootstrap method links, class/name indices. Strict mode rejects malformed semantic references; Compatible mode may retain an opaque essential representation or report a controlled error when safe recovery is impossible.
7. **Finalize**: collect structured diagnostics, record recovery/opaque flags and return only after the object is internally coherent.

## Version and historical compatibility

The class-file version is a pair (`major`,`minor`), not a single monotonically increasing Java-language version. Support the documented historical 45.x range and explicitly recognize that **some pre-release 45.x class-file dialects are not identical to the later standard**. Build compatibility branches from actual fixtures and archived specifications, not from assumptions based on "Oak" as a name. In particular, distinguish support for standard 45.0–45.3 files from experimental alpha dialects whose CP/attribute details may differ.

- Known standard CP tags or attributes that are "too new" for the declared version must not cause unsynchronized parsing. CP tags still have fixed known lengths, so they can be read with a diagnostic in Compatible mode, but unknown CP tags remain fatal.
- An attribute that is known in a *newer* specification is treated as inapplicable when the declared version predates it. Do not call that new codec just because its name matches.
- Preserve unknown access flag bits and other historical fields in compatible mode; strict validation is version-sensitive.
- If a version-specific dialect adapter is necessary, isolate it in `io/` and make it fixture-tested; don't contaminate all contemporary decoders with unchecked heuristics.
- Never claim universal Oak support unless regression fixtures for those concrete formats pass.

## Diagnostics and recoverability

Use a structured diagnostic, for example:

```cpp
struct Diagnostic {
    Severity severity;         // Info, Warning, Error
    DiagnosticCode code;       // DroppedMalformedAttribute, VersionInapplicable, ...
    size_t offset;             // Input byte offset when applicable
    std::string path;          // e.g. methods[2].Code.attributes[1]
    std::string detail;
    RecoveryAction action;     // Dropped, PreservedOpaque, RetainedRaw, None
};
```

Fatal `ReadError` includes offset, context path, kind, and enough detail to reproduce. At minimum define: `InvalidMagic`, `TruncatedInput`, `InvalidConstantPool`, `UnknownConstantTag`, `InvalidIndex`, `AttributeOverrun`, `MalformedEssentialCode`, `InvalidBranchTarget`, `ResourceLimitExceeded`, `UnsupportedDialect`.

The caller must be able to determine which attributes were lost. Compatibility warnings are emitted even when parsing succeeds. Put a warning count cap into limits to prevent diagnostics themselves becoming a memory exhaustion vector.

## Example: poisoned annotation attribute on an old class

```text
ClassFile.version = 45.x
ClassFile.attributes:
  SourceFile  [valid, length=2]
  RuntimeVisibleAnnotations [declares 4 bytes of nonsensical payload]
  Deprecated  [valid, length=0]
```

When the annotation attribute was introduced after the declared class-file version, Compatible mode **does not parse those 4 bytes as annotation data**. It consumes exactly the bounded slice, emits `VersionInapplicable`/`Dropped`, then parses `Deprecated` normally. If the declared `length` itself extends beyond file bounds, neither mode invents a boundary; it returns `AttributeOverrun`.

## Essential-data policy

Treat `Code` and `BootstrapMethods` (when needed by dynamic constants/invocations) as potentially semantics-critical. Do not simply discard a bad Code attribute from a concrete method and claim the result is valid. If its outer envelope is intact but the body is unsupported, `OpaqueCode` may be preserved with a diagnostic and a limited read-only API; the writer must make its restrictions explicit. If critical linkage cannot be preserved or understood, return a structured error instead of producing a plausible but broken class.

## Robustness tests that must exist

Valid attribute before and after malformed one; malformed nested Code attribute; an attribute with a too-large length; malformed name index; future-known attribute on old class; unknown vendor attribute preserved; truncated CP; unknown CP tag; malformed MUTF-8; switch counts requiring absurd allocation; branch into middle of instruction; mismatched declared lengths; duplicate attributes; unusual 45.x files; parse/write after the caller frees the original data vector. Test both Strict and Compatible outcomes.
