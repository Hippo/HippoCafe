# 03 — Class Model, Constant Pool, and Attributes

## Design principles

A class file is simultaneously a **semantic object model** and a **versioned binary container**. Pass one must expose sensible high-level objects while retaining enough original structure to round-trip files containing duplicate CP entries, unknown attributes, unusual attribute order and legacy quirks.

Therefore: (1) semantic convenience APIs are **views over one canonical owner**; (2) physical constant-pool identity is stable; (3) a single ordered attribute list is the source of truth; (4) explicit raw/opaque representations are first-class, not error cases.

## Core value model

```cpp
namespace cafe {

struct ClassVersion {
    uint16_t major;
    uint16_t minor;
};

struct ClassFile {
    // Private stable storage context and owned source bytes.
    ClassVersion version;
    uint16_t access_flags;           // Preserve unknown bits in compatibility mode.
    CpIndex this_class;
    CpIndex super_class;             // Zero is meaningful for java/lang/Object and modules.
    std::pmr::vector<CpIndex> interfaces;
    ConstantPool constant_pool;
    std::pmr::vector<Field> fields;
    std::pmr::vector<Method> methods;
    std::pmr::vector<Attribute> attributes;
};

struct Field {
    uint16_t access_flags;
    CpIndex name_index;              // Must refer to CONSTANT_Utf8.
    CpIndex descriptor_index;
    std::pmr::vector<Attribute> attributes;
};

struct Method {
    uint16_t access_flags;
    CpIndex name_index;
    CpIndex descriptor_index;
    std::pmr::vector<Attribute> attributes;

    // code() returns a view/reference to the unique Code attribute if present.
};

} // namespace cafe
```

These sketches show *semantics*, not literal required public data members. Prefer private invariants and accessor methods to exposing mutable fields that could invalidate CP references. Support zero/null CP indexes only in positions where the JVMS permits them. Access flags are stored as raw `uint16_t` plus typed bitmask helpers; never invent validation rules from later class-file versions for older files.

A programmatic constructor must supply a valid declared class version and class identity; a parsed instance must retain exactly the declared version and accessibility bits. No silent upgrades to a current version.

## Constant pool: identity is the foundation

**Pass-one rule: original CP indexes remain stable forever.** This solves a real preservation problem: unknown attributes and original bytecode may contain CP indices that we cannot discover and rewrite safely.

```cpp
struct CpIndex { uint16_t value; }; // Index 0 is reserved/sentinel, when allowed.

enum class CpTag : uint8_t {
    Utf8 = 1, Integer = 3, Float = 4, Long = 5, Double = 6,
    Class = 7, String = 8, Fieldref = 9, Methodref = 10,
    InterfaceMethodref = 11, NameAndType = 12,
    MethodHandle = 15, MethodType = 16, Dynamic = 17,
    InvokeDynamic = 18, Module = 19, Package = 20
};
```

The actual CP representation should use compact tagged entries with inline fixed-width fields, plus out-of-line UTF-8 bytes where needed. For example, `CONSTANT_Methodref` stores `class_index` and `name_and_type_index` as 16-bit indices, not duplicated owner/name/descriptor strings. The following `Long` / `Double` padding slot consumes an index but does not contain an encoded entry. CP count is u2 and includes the unused zero slot; validate additions against that limit.

**Preserve:**

- Original CP entry order, duplicate entries, unused entries and long/double second slots.
- Exact `float`/`double` IEEE-754 bit patterns (including NaN payload bits), rather than round-tripping through host floating-point values.
- Original modified-UTF-8 (`CONSTANT_Utf8`) byte sequences, even if lazy text decoding fails in compatibility mode. Do not replace modified UTF-8 with standard UTF-8 or normalize surrogate pairs on the wire.
- Method handle reference kind, distinction between Methodref and InterfaceMethodref, InvokeDynamic/Dynamic bootstrap index, and module/package entry tags.

**Appending:** `ConstantPool::add_*` returns a stable `CpIndex`; optional deduplication for new entries is permitted. **Do not** deduplicate existing source entries or rewrite their indices. Updating a referenced symbol should generally append a new entry and retarget the owning reference; modifying an original entry in place may change multiple unrelated references or opaque payload semantics and should not be the default API.

Offer typed read access such as `get_utf8(CpIndex)` and `get_method_ref(CpIndex)` with recoverable errors for mismatched tags/invalid indices. Expose `std::string_view` only while backed by owned storage; the view must not be cached beyond the class's lifetime. If a semantic string is not valid as decoded Java modified UTF-8, expose its raw bytes and a diagnostic instead of silently substituting text.

**Why not immediately replace CP indices with globally interned symbolic IDs?** A semantic symbol repository can be added above the pool later, but reparsing raw attributes requires stable original physical indices. In pass one, a typed `CpIndex` is the stable bridge between both views. A writer may add interning for new entries without compacting the original pool.

## Attributes are ordered, owned entries

All five attribute-bearing contexts use the same abstraction: `ClassFile`, `Field`, `Method`, `Code`, and `Record` component (when Record is typed). Each parsed attribute has a valid wire header (`name_index`, `length`) and one payload disposition:

```cpp
struct SourceSlice { uint32_t offset; uint32_t length; };

struct RawAttribute {
    // Either SourceSlice into ClassFile-owned input or newly owned byte storage.
    AttributeBody bytes;
};

struct Attribute {
    CpIndex name_index;
    AttributePayload payload; // tagged typed body OR RawAttribute
    // Optional provenance: original source offset, recognized type, etc.
};
```

`AttributePayload` is an internal tagged union / variant at the ATTRIBUTE level; that is fine. The restriction against large `std::variant` concerns every instruction, not the relatively few attributes.

**One canonical representation:** Do not store `Method::max_stack` separately from `Method::Code` or `ClassFile::source_file` separately from a SourceFile attribute and allow them to disagree. High-level convenience methods may return a view into the authoritative attribute entry. Preserve attribute order and duplicate occurrences when legal; validate uniqueness according to each attribute's rules in strict mode. In compatibility mode, duplicate or malformed nonessential attributes may be dropped with diagnostics rather than fatal error if safely bounded.

### Typed vs opaque attributes

The first pass should type the attributes needed to understand class structure and bytecode: `Code`, `ConstantValue`, `Exceptions`, `SourceFile`, `Signature`, `BootstrapMethods`, and simple marker attributes. Basic debug scopes may be typed when useful. All *other standard attributes* must still survive parsing and writing using bounded opaque payloads when they cannot yet be safely decoded, including annotations, frames, records and module metadata; see the coverage contract in `07-format-coverage.md`.

Raw attributes are **not** assumed semantically meaningless. A raw attribute might contain constant-pool references, instruction offsets or ranges. An append-only CP makes CP references stable, but does **not** make code offsets stable after bytecode edits.

For malformed attributes in a compatible read, keep a `Diagnostic` and omit them from the canonical model (unless a specifically documented opaque recovery policy applies to essential payloads). This prevents the writer from quietly recreating the same structurally broken attribute.

## Code ownership and metadata

A `Method` has zero or one `Code` attribute when permitted by the format. `CodeAttribute` contains:

```cpp
struct CodeAttribute {
    uint16_t max_stack;   // Parsed as-is, never recomputed in pass one.
    uint16_t max_locals;  // Parsed as-is.
    MethodCode code;
    std::pmr::vector<ExceptionHandler> exception_handlers;
    std::pmr::vector<Attribute> attributes; // includes raw StackMapTable, debug attrs
};

struct ExceptionHandler {
    LabelId start;  // inclusive
    LabelId end;    // exclusive, may denote code_length
    LabelId handler;
    CpIndex catch_type; // zero means catch-all
};
```

The nested `Code` attributes are retained in original order. `StackMapTable` is **preserved**, not synthesized. `LineNumberTable`, `LocalVariableTable`, local-variable type info and code-targeted type annotations contain offsets/ranges and must either be transformed with code or invalidated on edits. In pass one, preserve these without re-derivation for unchanged code.

Raw original `Code` bytes can be retained separately for an unchanged fast path. A method with an unsupported yet well-bounded instruction sequence may be represented as `OpaqueCode` in compatibility mode; it is inspectable as bytes but cannot expose decoded instructions or undergo code edits. Don't pretend it is decoded.

## Class model edge cases

- `java/lang/Object` and `module-info` can legitimately have `super_class == 0` in their applicable contexts.
- `Code` is not valid for abstract/native methods; tolerate and diagnose anomalies according to read policy rather than assuming every method has bytecode.
- Interfaces, annotation types, enums, modules, records and sealed class metadata are version-sensitive; don't collapse them to one fixed era's rules.
- `CONSTANT_Dynamic` and `CONSTANT_InvokeDynamic` reference entries in `BootstrapMethods`. Preserve both index spaces without accidentally treating bootstrap method indices as CP indices.
- A class may contain attributes whose current specification defines no semantic effect; preserve or drop based on declared policy, not a blanket rule that unknown means corruption.
- Use `int32_t` for switch keys/branch displacements and checked wider arithmetic for offset calculations.
- Preserve method/field declaration ordering, attribute ordering, interface ordering and unused CP entries for round-trip fidelity.

## Model validity states

Distinguish at least these states:

1. **ParsedClean:** no recovery or unsupported essential payloads.
2. **ParsedRecovered:** safe, explicitly reported attribute drops or nonfatal format deviations; contents may not be JVM-loadable.
3. **OpaqueEssentialContent:** a crucial item such as Code could not be decoded but is length-bounded and preserved verbatim; writer restrictions apply.
4. **EditedCode:** bytecode layout or semantics changed; original derived metadata must not be presumed valid.

These may be flags rather than a single enum, but must not be ambiguous. `ClassFile` is a representation, **not** proof that a JVM verifier accepts it.
