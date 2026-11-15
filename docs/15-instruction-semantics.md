# 15 — Instruction Simulator and Opcode Semantics

## Scope

Implement **complete verification-relevant effects** for every JVM instruction that the library claims to analyze. All effects are derived from the canonical normalized `Instruction` (8 bytes plus out-of-line operands), the method's constant pool/descriptors, the current `Frame`, and the resolution context where required. Parsing a bytecode is not evidence that its semantics are supported by the analyzer.

A single opcode metadata catalogue should drive classification and operand validation where possible, but instruction simulation cannot be reduced to a constant stack delta: many instructions depend on descriptors, category-one/two operands, array component types, object initialization state, and verifier rules.

## Core API

```cpp
std::expected<Frame, AnalysisError> simulate_instruction(
    const ClassFile& owner,
    const Method& method,
    InstructionId at,
    const Instruction& instruction,
    const Frame& in,
    const ClassHierarchy& hierarchy,
    std::pmr::memory_resource* scratch
);
```

Avoid per-instruction heap allocations when simulating common opcodes. The implementation can update an internal mutable frame in place and return a status instead; the signature is an API direction. Descriptors should be parsed into cached lightweight shapes, not reparsed into heap objects for every invocation.

## Required opcode groups

| Group | Required semantics / pitfalls |
|---|---|
| Constants (`aconst_null`, `iconst`, `bipush`, `sipush`, `ldc`) | Push correct verifier type; `ldc`/`ldc_w`/`ldc2_w` use CP tag and dynamic-constant descriptor; category-two constants consume two slots |
| Loads and stores (`xload`, `xstore`, `_n`, `wide`) | Check local-slot bounds and type; preserve category-two pairs; storing into overlapping slots invalidates the appropriate previous pair |
| `iinc` | Requires integer local, correct normalized signed increment and wide index handling; no stack change |
| Stack-manipulation (`pop*`, `dup*`, `swap`) | Implement every legal category-one/category-two form; reject illegal shapes; no generic "copy top N slots" shortcut |
| Arithmetic, bitwise, negation and conversion | Exact pop/push types and widths; shifts consume `int` count; comparisons produce `Integer` |
| Conditional branches / `goto` | Consume properly typed operands; validate reference comparison rules; transfer frame to taken/fallthrough targets |
| Switches | Consume an integer; validate payload and targets; outgoing case/default edges share the post-pop frame |
| Returns | Match the method's declared return type and category; return with proper constructor initialization state; no fallthrough |
| Field get/put | Resolve symbolic owner/name/descriptor as needed, check receiver/value types and static-vs-instance use; widths from descriptor |
| Invoke (`virtual`, `special`, `static`, `interface`, `dynamic`) | Pop descriptor arguments right-to-left; receiver where applicable; push return value; verify type/state requirements, including `<init>` and interface invokes |
| Allocation (`new`, `newarray`, `anewarray`, `multianewarray`) | `new` yields allocation-site uninitialized; arrays consume dimensions and produce appropriate array reference type |
| Array loads/stores/length | Validate array reference, component kind, index type and loaded/stored value; handle `null` and primitive/reference array differences |
| `checkcast`, `instanceof` | Correct reference input and result (reference target / Integer); dynamic hierarchy checks as required |
| `athrow`, monitors | Enforce reference operand constraints, exception/monitor effects; no normal successor after `athrow` |
| Reserved/invalid opcodes | Must not simulate as no-op; explicit unsupported/invalid result |
| `jsr`/`ret` | Legacy-only subroutine state; reject from modern analysis or dispatch to a dedicated supported legacy implementation |

This table is a checklist, not a substitute for the JVMS instruction-specific verification rules. Implement every recognized logical opcode or classify it as explicitly unsupported under the selected class-file version. Do not ship a partially implemented default case that silently produces zero effect.

## Descriptor-dependent effects

Build one bounded JVM descriptor parser shared with the class model, classpath metadata, and simulator:

- Field descriptor -> kind and slot width.
- Method descriptor -> parameter sequence (including array/reference identities), parameter-slot total, and return kind/width.
- `ldc` of `CONSTANT_Dynamic` -> type obtained from the dynamic constant's field descriptor, not a generic Object.
- `invokedynamic` -> descriptor from its `NameAndType`, no normal instance receiver; bootstrap data is not invoked or executed by analysis.
- `invokeinterface` -> receiver required; follow the specification's operand and version constraints, not the class source language.
- `new`, field and method references -> validate constant-pool entry types and declared descriptors rather than relying on caller-packed integer fields.

Use classpath resolution only where type relationships must be proven; do not demand a full hierarchy for a stack-height-only effect that depends solely on a valid descriptor.

## Category-two stack manipulation examples

```text
[Long]          dup2       -> [Long, Long]           valid
[Int, Int]      dup2       -> [Int, Int, Int, Int]   valid
[Long]          swap       -> error                 invalid category shape
[Int]           pop2       -> error                 insufficient category width
[Long]          pop2       -> []                    valid
```

Here `[Long]` is one logical operand-stack entry of **two JVM slots**, not two independently movable values. Enumerate exact `dup_x2`, `dup2_x1`, and `dup2_x2` forms from the JVM specification, with targeted unit tests for each legal and illegal form.

## Local-variable and constructor concerns

- `LLOAD i` uses slots `i` and `i+1`; `LSTORE i` writes both and invalidates overlapping previous wide values.
- `ALOAD i` may load a verifier reference or allowed uninitialized reference; improper uses of uninitialized objects still fail at later instructions.
- `INVOKESPECIAL <init>` is not equivalent to an ordinary method call. Verify the receiver's uninitialized identity and permissible constructor target, then transition every alias of that identity in locals/stack to initialized state on normal return.
- For handlers over such instructions, the **pre-instruction** frame is propagated before the normal initialization transition.
- Constructor exit requires valid initialization state, subject to JVM version rules.

## Static effect table vs. type-sensitive engine

Use `OpcodeInfo` to provide:

```cpp
struct OpcodeInfo {
    OperandForm operand_form;
    ControlFlowKind flow;
    bool may_throw;
    // static width effects only for opcodes where they are genuinely constant
};
```

A fast max-stack mode can use direct width effects for constants/arithmetic, descriptor-cached widths for calls/fields, and category tracking for `dup*`/`pop*`. A full frame mode uses exact reference verification types. Both modes must agree on stack-slot maxima for valid methods; test this invariant.

## Unresolved references and policy

Where a reference relationship is required, a missing hierarchy definition is an error. Do not substitute Object, assume every `CHECKCAST` succeeds in every context, or fabricate a method descriptor. Where the opcode merely carries a symbolic type that need not be resolved to determine stack widths, leave resolution lazy.

An optional diagnostic/approximate analysis mode may be added separately, but it must be marked non-verification-safe and the writer must refuse to use it for `ComputeFrames`.

## Testing and benchmarking

- Table-driven tests for every supported opcode and wrong operand-kind cases; compare stack shape, types, locals and maximum occupied slot.
- Exhaustive `dup*`/`pop*` category form tests and category-two local overwrite tests.
- `ldc` constant tags including dynamic primitives/long/double; interface calls, constructors and invokedynamic.
- Arrays including null references, primitive and reference components and nested arrays.
- Invalid method descriptors, invalid CP tags, invalid call receiver, wrong return type and poisoned switch payloads.
- Benchmarks for straight-line arithmetic, frequent field/method calls, many small methods and large switch-heavy methods; record allocations per simulated instruction.

## References

JVMS §4.3 (descriptors), §4.4 (CP entries), §4.9 (constraints), §4.10.1 (verification rules), and §6 (opcode definitions).
