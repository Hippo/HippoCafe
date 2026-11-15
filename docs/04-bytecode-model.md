# 04 — Instruction and MethodCode Representation

## A compact, independently editable instruction model

The canonical instruction occupies eight bytes, independent of JVM opcode:

```cpp
enum class Opcode : uint8_t { Nop = 0x00, IAdd = 0x60, IInc = 0x84,
                             IfEq = 0x99, TableSwitch = 0xaa,
                             LookupSwitch = 0xab /* ... */ };

struct Instruction {
    uint32_t operand = 0;   // signed immediate bit pattern, CP index, label or payload ID
    uint16_t auxiliary = 0; // second small operand, when required
    Opcode opcode = Opcode::Nop;
    uint8_t encoding = 0;   // 0 = automatically choose a legal wire encoding
};

static_assert(sizeof(Instruction) == 8);
static_assert(std::is_trivially_copyable_v<Instruction>);
```

`Opcode` values must fit one byte. Distinguish **logical** opcodes from wire encodings: e.g. `Iload0`, `Iload`, and `wide iload` decode into logical `Opcode::ILoad` plus local index. Encoding preferences are optional hints; the writer must ignore a hint if it cannot emit legal bytecode. Keep an explicit opcode metadata table (`operand form`, `legal version`, `wire widths`, `control-flow classification`) rather than virtual per-opcode subclasses.

### Operand interpretation

| Logical form | `operand` | `auxiliary` | Examples |
|---|---|---|---|
| None | unused | unused | `IAdd`, `Return` |
| Signed immediate | encoded signed bits | unused | `Bipush`, `Sipush` |
| Local | zero-extended local index | unused | `ILoad`, `ALoad`, `Ret` |
| Increment | sign-extended 16-bit increment bits | 16-bit local index | `IInc` |
| CP reference | `CpIndex.value` | usually unused | `Ldc`, `GetField`, `InvokeVirtual`, `New` |
| Branch | `LabelId.value` | unused | `IfEq`, `Goto`, `Jsr` |
| Table/lookup switch | method-local switch payload ID | unused | `TableSwitch`, `LookupSwitch` |
| Multi-dimensional array | CP Class index | dimensions (`u1` on disk) | `MultiANewArray` |

An `Instruction` must only be created through validated constructors or validated on insert; a generic raw constructor may exist internally for the decoder. Public API should expose typed builders/accessors (`Instruction::load`, `Instruction::branch`, `code.branch_target`) rather than asking every caller to manually pack bit fields.

Version-sensitive opcodes and historical `jsr`/`ret` must be understood: do **not** delete them because modern compilers rarely emit them. Handle `wide`, `tableswitch`, `lookupswitch`, `invokeinterface` extra count/zero and `invokedynamic` trailing zero operands correctly. Reserved/invalid opcode bytes in a Code stream are not converted into fake no-ops; either fail decoding or preserve the whole Code attribute as explicitly opaque when framing allows it.

## MethodCode storage

```cpp
struct InstructionId { uint32_t index; uint32_t generation; };
struct LabelId { uint32_t value; };
struct SwitchId { uint32_t value; };

class MethodCode {
public:
    InstructionId append(Instruction value);
    InstructionId insert_before(InstructionId at, Instruction value);
    void erase(InstructionId id);
    Instruction get(InstructionId id) const;
    void replace(InstructionId id, Instruction value);
    LabelId make_label();
    void bind_label(LabelId id, CodePosition position);
    // iteration follows the `order` vector, not physical slot index.
};
```

Implementation: method-local slot array with generation, freelist and an ordered vector of 32-bit slot indices. A 64-bit `InstructionId` is stable across append/insert/reorder but becomes invalid after erase/recycle. IDs are scoped to a method—never resolve the same handle against another method. If IDs can be accidentally transported across methods, use a method owner token in debug validation or an explicitly scoped API.

Handle lookup checks index, occupied state and generation. Do not expose long-lived pointers into a movable slot vector. A `replace()` that preserves instruction position can retain identity but must release the previous out-of-line payload if the operand kind changes.

Order-vector insertion and deletion are O(n) in the number of instructions shifted, but traversal is cache-friendly and `Instruction` itself remains small. Keep this initial representation until benchmarks justify indexed links/slabs.

## Labels are boundaries, not byte offsets

Every label denotes a **boundary** before a specific instruction or the exclusive end of the method. A label may be used for a branch or metadata range, but only a valid instruction-start boundary is a legal executable branch target. `end` boundaries are allowed for exception handler/range ends, not handler targets or goto destinations.

Labels are separate method-local objects. During read: first decode instructions with original byte offsets; collect branch and metadata targets; map each valid target offset to a stable label (prefer same label for the same position). Reject invalid mid-instruction targets in strict mode. A compatible reader may retain an opaque Code instead of inventing a valid target. Finally bind labels to instruction boundaries. Offset calculations use checked signed 64-bit intermediates.

Specify insertion semantics carefully:

- `insert_before(id, value)` inserts **before** the old instruction and its label-boundary semantics must remain explicit.
- `insert_at(label, value)` inserts code that should execute when branching to the label, while preserving the label's intended target.
- Deleting a labeled instruction requires an explicit retarget policy or an error; never silently strand branch references.

Do not store labels as phantom opcode instructions that inflate every code sequence. A separate label table plus boundary map is simpler for serialization and keeps the 8-byte instruction array pure.

## Switches: out-of-line operands

```cpp
struct TableSwitch {
    int32_t low;
    LabelId default_target;
    std::pmr::vector<LabelId> targets; // high = low + size - 1, checked
};

struct LookupPair {
    int32_t key;
    LabelId target;
};

struct LookupSwitch {
    LabelId default_target;
    std::pmr::vector<LookupPair> pairs; // strictly ascending signed keys
};
```

The 32-bit operand of the instruction points into a method-owned switch payload repository. Prefer generation-safe or non-reused IDs where possible. Parsing checks declared table sizes before allocating (`high >= low`, no signed overflow, payload bytes are in-bounds). Emission recalculates padding based on bytecode offset and computes signed 32-bit displacements from the **switch opcode start**. The default target is always explicit. Use no per-case heap objects.

## Code contents and original-wire representation

`CodeAttribute` owns `MethodCode`, exception handlers, original maxes, nested attributes, and optionally a raw original `Code` payload or code-byte slice. Each `MethodCode` records a monotonic mutation version or dirty state whenever its order, operands, labels, exception boundaries or effective encoding changes. Any mutable accessor must either mark dirty or require an explicit mutation operation; returning unchecked mutable references can break invalidation.

**Fast path:** unchanged Code emits original bytes, preserving exact switch padding, instruction encoding choices and code-offset metadata. The class can be written with appended CP entries and other unrelated edits because original CP indices remain identical.

**Re-encode path:** run a separate layout pass, then encoder. This path must not silently reuse raw code-offset-bearing attributes from a dirty method. Safe emission of rewritten Code without recomputation requires explicit metadata supplied/validated by the caller; see `06-writer-and-roundtrip.md`.

## Encoding / layout edge cases

- `ldc` vs `ldc_w` vs `ldc2_w`; `wide` local accesses and `iinc`.
- Conditional branch displacement range (signed 16-bit) and `goto_w`/`jsr_w` (signed 32-bit); calculate layout until stable if widening is supported.
- `tableswitch` and `lookupswitch` padding depends on final offset, so layout and branch widening can interact. Iterate to a fixed point with an iteration guard or return `UnstableLayout`.
- Conditional branches do not have a built-in wide form. Supporting long branches requires a semantics-preserving inverted-condition plus `goto_w` expansion, or an explicit `BranchOutOfRange` error. Never truncate displacement.
- `newarray` uses an `atype`, not a CP index; `multianewarray` needs dimensions; `invokeinterface` count and reserved byte must be correct; `invokedynamic` has two required zero bytes.
- Reject a code array exceeding the JVM's u2-bounded maximum of 65535 bytes (including legal edge cases). Check every instruction start/end and exception range after final layout.
- Keep original opcode semantics for legacy subroutines (`jsr`/`ret`) when parsing, even if future transforms do not support them.

## No analysis in this pass

Do not compute stack heights, verification frames, local allocation or maxes. Do not conflate a bytecode sequence that can be encoded with a bytecode sequence that passes JVM verification. The writer's default policy must reject dirty methods with metadata it cannot safely preserve. `MethodCode` is not an SSA or CFG representation.
