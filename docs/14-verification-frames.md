# 14 — JVM Verification Frames and Data-Flow Analysis

## Goal

Compute **semantically correct JVM type states** at required bytecode boundaries, then provide those states for `StackMapTable` encoding after final code layout. The analyzer operates on normalized `MethodCode` instructions; it does not store byte offsets as canonical branch targets and it does not use the host JVM to compute types.

Modern stack-map verification is governed by the JVMS. Frame inference is an implementation method for producing valid declared frames, not a license to weaken the JVM's type-checking constraints. The output must be checked against the target class-file version's verification requirements.

## Internal verification types

```cpp
enum class VerificationKind : uint8_t {
    Top,
    Integer,
    Float,
    Long,
    Double,
    Null,
    UninitializedThis,
    Object,
    Uninitialized,
    ReturnAddress,   // legacy-only, not a StackMapTable wire tag
};

struct VerificationType {
    VerificationKind kind;
    uint32_t data;  // context-scoped reference type or NEW site, as applicable
};
```

This is an illustrative shape; the implementation may need richer temporary reference constraints during interface joins. `Boolean`, `Byte`, `Char`, and `Short` are represented as verifier `Integer`. `Object` covers class, interface, and array reference types. `Null` is its own special verification type. `Uninitialized` identifies a **specific `new` instruction site**, preferably with a stable `InstructionId`/dedicated site handle rather than a byte offset; the encoder resolves that site's final byte offset.

`ReturnAddress` exists only for legacy `jsr`/`ret` handling and is **never serialized as a modern StackMapTable verification_type_info**. Unsupported subroutine analysis must fail explicitly.

## Frame representation

```cpp
struct Frame {
    std::pmr::vector<LocalSlot> locals;
    std::pmr::vector<VerificationType> stack;
    uint32_t stack_slots;  // sum of logical operand widths; checked
    // optional verifier flags, such as constructor-initialization state
};
```

Store local variables by **JVM local-variable slots**. A `long`/`double` occupies two slots, and the implementation must distinguish its reserved second slot from an independently usable local. A private continuation marker is helpful; the writer converts it into the appropriate **wire verification-type list**, where category-two values implicitly occupy two variable locations. `Top`/unusable locals, gaps, and overwritten halves of category-two values must be modeled correctly.

Store operand-stack entries as logical values with width 1 or 2, or choose a consistent slot-level representation. In either case, `dup*`, `pop2` and `swap` operate on valid category shapes, and `max_stack` counts slots rather than number of logical stack entries. Use checked arithmetic and reject illegal underflow or category splitting.

Frame values must be owned by a caller-controlled analysis lifetime or by a self-owning result. A reference to resolver-owned `TypeId` also has a resolver-snapshot lifetime. A frame cannot survive unchanged method semantics or changed hierarchy identity without explicit revalidation.

## Initial method frame

Derive the initial locals from the method descriptor and flags:

- Instance methods receive `this` in slot 0; static methods do not.
- A constructor `<init>` ordinarily begins with `UninitializedThis`, subject to the class/version rules for root `java/lang/Object` and applicable initialization cases.
- Each parameter occupies one slot, except `long`/`double`, which occupy two.
- Array parameters are array reference types, not bare Object unless the descriptor actually says Object.
- The operand stack begins empty.
- Validate descriptor syntax, allowed parameter-slot count, and method/access-flag consistency before analysis.

The entry frame is implicit for `StackMapTable`. Do not emit a redundant explicit frame at bytecode offset 0 merely because the CFG labels it as a leader.

## Worklist algorithm

A recommended forward data-flow algorithm:

```text
cfg = build_cfg(method.code, handlers)
in_state[entry] = initial_frame(method)
worklist.push(entry)

while worklist not empty:
    block = worklist.pop()
    state = copy(in_state[block])
    for instruction in block in bytecode order:
        pre_state = state
        propagate_exception_edges(instruction, pre_state, handlers)
        state = simulate(instruction, state)
        max_slots = max(max_slots, state.stack_slots)
    for normal successor of block:
        next = merge(in_state[successor], state)
        if next changed:
            in_state[successor] = next
            worklist.push(successor)

validate_required_boundaries_and_exit_states()
return analyzed_entry_states_and_max_stack()
```

The pseudocode omits error handling and optimization. **Worklist convergence** needs a finite verification lattice, identity-safe allocation-site handling, deterministic joins, a resource/iteration budget, and no oscillation. Every merged frame must be compatible with the verifier's type system. Duplicate edges may merge into one state without losing exceptional semantics.

### Merge rules

- Operand-stack *heights and category shapes* at a join must be mutually compatible. A mismatch is an error; never pad the operand stack with `Top`.
- Matching primitives retain their type; incompatible primitives on the operand stack are errors. For local variables, incompatible types may merge into unusable (`Top`) under verifier-appropriate rules.
- `Null` plus an initialized reference can merge to that reference.
- Two reference types require a hierarchy-informed representable join; special treatment for interfaces and arrays is specified in `12`.
- Uninitialized references merge only when their allocation-site/constructor identities and the applicable verification rules permit it. Never coerce a not-yet-initialized object to its allocated class merely to make a join succeed.
- A local written over one half of a `long`/`double` invalidates the other half as required by the JVM's local-variable rules.
- Keep any constructor-specific initialization flags consistent across control flow, including exceptional edges.

### Object initialization and aliasing

`new C` pushes a unique uninitialized reference. A permitted `invokespecial C.<init>` converts the appropriate aliases of **that same allocation site** to initialized `C`, including other locals/stack entries referring to the object. The verifier has restrictions on how uninitialized objects may be used before initialization; implement them instead of treating uninitialized references as ordinary Object values. `UninitializedThis` has separate rules for constructor delegation and superclass initialization.

For exception handlers covering constructor calls, propagate the appropriate **pre-instruction** state and enforce the special constructor rules. A handler cannot be given an initialized local merely because the normal successor of an `invokespecial <init>` has become initialized.

## Required frame points and unreachable instructions

Generate candidate semantic frames for required control-flow boundaries: branch/switch targets, exception-handler entries, and other verifier-required block entries (including appropriate boundaries after unconditional transfer). Preserve the relationship to stable labels/instruction IDs until emission.

Only reachable states have been proven by the worklist. **Do not manufacture valid-looking frames for unreachable blocks**. If dead code requires handling to pass modern type checking, explicitly model the target JVM's requirements and test them, or return a diagnostic indicating that this code needs normalization/removal. No silent dead-code deletion, zero-filled frames, or speculative initialized Objects.

## Frame selection and validation

A semantic analysis result is not automatically a valid StackMapTable. Before returning verification-safe success:

1. Ensure the selected types are serializable as the target version's frame types.
2. Ensure incoming state at every stated frame is assignable to that declared frame.
3. Recheck the instruction stream against the selected declared frames, especially where interface ambiguity was conservatively resolved.
4. Ensure each required entry is represented and frame offsets are strictly ascending after writer layout.
5. Ensure all `Uninitialized` sites point to a surviving `new` instruction in the same method.
6. Ensure no legacy-only `ReturnAddress` is present in a modern frame.

It is acceptable to begin with `full_frame` encoding and add compression later; that affects size, not semantic validity. The `FrameAnalyzer` returns a **semantic** result. The writer owns encoding tags, CP `CONSTANT_Class` indices, byte offsets, and `offset_delta` calculations.

## Version behavior

- For class-file versions using modern `StackMapTable` verification (starting at major 50), compute appropriate frames when requested and generate a valid table if necessary. Do not rely on the optional historical type-inference fallback for major 50.
- For older versions using verification by type inference, compute/maximize legacy-compatible metadata as supported, but do not inject an invalid modern `StackMapTable` into an older class.
- Legacy `jsr`/`ret` cannot be represented as modern stack-map return-address types. Preserve existing decoding and unchanged round-trip behavior; unsupported computation returns a specific error.
- Support for new verification features must be gated by class-file version and specification, not by the age of the host Java runtime.

## Diagnostics and tests

Use `AnalysisError` with at least class, method descriptor, instruction/label, error kind, expected/actual type or stack shape, and relevant source/type-resolution cause. Distinguish `StackUnderflow`, `InvalidCategoryOperation`, `IncompatibleJoin`, `UninitializedUse`, `InvalidConstructorState`, `TypeResolutionFailure`, `UnrepresentableFrame`, and `UnsupportedSubroutine`.

Test explicit expected states for constructor paths, aliases of uninitialized allocations, handler pre-states, primitive/reference joins, arrays/interfaces, category-two locals, `dup2_x2` forms, unreachable blocks, and loop convergence. Cross-check generated classes through a separate JVM verifier; self-round-trip is not enough.

## References

JVMS §4.7.4 (StackMapTable), §4.9.2 (structural constraints), §4.10.1 (modern verification), §4.10.2 (legacy inference), §6 (individual opcodes). Their per-version rules are authoritative over illustrative pseudocode in this document.
