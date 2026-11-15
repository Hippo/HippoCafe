# 13 — Control-Flow Graph and Exception Edges

## Responsibility

Construct an immutable, version-stamped control-flow view over `MethodCode` for stack-height and frame analyses. The CFG uses existing `InstructionId`, `LabelId`, method ordering and exception-table ranges; it does not allocate new canonical instructions or take ownership of the method.

The CFG must represent **normal successors** and **exceptional transfer**. A basic-block-only adjacency list is not sufficient to capture every exception-handler frame correctly unless per-instruction protected-range state is retained.

## Model

```cpp
struct BlockId { uint32_t value; };

enum class EdgeKind : uint8_t {
    Fallthrough, Conditional, Unconditional, SwitchCase, SwitchDefault,
    Exception
};

struct Edge {
    BlockId target;
    EdgeKind kind;
    // Exceptional edges also carry handler identity/catch type and source range.
};

struct BasicBlock {
    InstructionId first;
    InstructionId last;           // inclusive, or use explicit half-open boundary
    std::pmr::vector<Edge> outgoing;
};

struct ControlFlowGraph {
    // Block array, instruction-to-block map, entry block, mutation version,
    // and handler/protected-range lookup needed for per-instruction propagation.
};
```

Choose a single precise interval convention, preferably half-open `[begin, end)` for bytecode and instruction order. Keep an end-of-code sentinel boundary for exception-range ends; it is not a legal executable target. A block consists of instructions, **not** label nodes, because labels are separate boundaries in `MethodCode`.

## Block boundaries

Create leaders at:

1. The method entry (first real instruction).
2. Every branch and switch target.
3. The instruction after conditional branches (fallthrough).
4. Valid instructions following `goto`, `return`, `athrow`, `tableswitch`, `lookupswitch` or other terminators, so dead spans remain separate.
5. Exception-handler entry labels.
6. Start and end of each protected exception range when needed to make block-based exceptional control flow precise.
7. Other implementation-required boundaries for correct reachability and stack-map frame placement.

Use the opcode metadata table to classify branches, returns, throws and fallthrough. Avoid logic that assumes unrecognized bytecodes always fall through. Reject branches into an instruction's operand bytes; parsed labels must denote instruction boundaries.

For each block, enumerate successors in deterministic order. A conditional has both taken and fallthrough edges. `goto` has its target only. A switch has default and case edges (deduplicate targets if desired without losing case association). Return/throw have no ordinary fallthrough. Falling off the end of a reachable nonterminating instruction sequence is an analysis error.

## Exception-handler semantics

A handler table entry contains a protected half-open range, target, and `catch_type` (zero means any exception; a `Throwable` reference is the verification stack type for catch-all). The exception table's original **order** matters for runtime matching and must be preserved by the writer.

For each instruction in a protected range as required by the applicable JVM verifier rules (not merely an approximation based on a best-effort `may_throw` flag):

- Create/recognize an exceptional transfer to each applicable handler, respecting the table's semantics and conservative verifier requirements.
- The handler's incoming operand stack contains **exactly one reference** to the appropriate catch type; its `max_stack` contribution is at least one slot when reachable.
- Its local-variable state comes from the instruction's **incoming (pre-instruction)** state, not from the block exit or the partially mutated post-instruction state.
- Preserve constructor/uninitialized-object distinctions when propagating exceptional states. This matters especially for `NEW`, `INVOKESPECIAL <init>`, and aliasing.
- Use a checked mapping from instruction order to containing handler ranges. A protected range may begin or end in the middle of an otherwise linear sequence; splitting or per-instruction mapping must cover that precisely.

Modeling exceptional type-state obligations across the full protected range is the safe default for frame verification. A `may_throw` classification may optimize other analyses, but it must **not** prune a verifier-required handler obligation without a specification-backed equivalence proof. The simulator must use pre-instruction state and must not create fabricated valid merges from incompatible states. Do not approximate the entire protected block with its terminal frame.

## Unreachable code

Preserve unreachable instructions and their original order. The CFG may represent blocks without an entry path; normal frame/max analysis must distinguish these from reachable blocks rather than filling them with arbitrary `Object` or zero stack state.

Generating modern stack maps can require attention to verifier expectations at boundaries after unconditional branches, even for apparently unreachable code. The writer/analysis pipeline must either model and emit a demonstrably acceptable frame, safely transform the code as explicitly requested, or return an error. It must not silently delete dead bytecode or invent type states to force success.

## Legacy subroutines

`jsr`, `jsr_w`, and `ret` are not ordinary single-target branches. Their return-address/subroutine semantics are distinct from modern CFG transitions. Keep decoding/round-trip support, but route subroutine-containing methods to an explicitly supported legacy analyzer or return `UnsupportedSubroutineAnalysis`. Never treat `ret` as a normal fallthrough or assume `jsr` returns to exactly one statically fixed successor without analysis.

## Complexity and memory

Build the block leader map with a sorted list or a dense instruction-position array, O(n + branches + handler boundaries) before edge construction. Avoid O(n × handler_count) materialization on adversarial methods: use interval sweeps or range indexes for protected-range queries. The graph should be PMR-backed in caller-owned scratch/session storage, not allocate one heap object per block or edge. Reject pathological handler/edge counts under configurable resource budgets.

Each graph captures `MethodCode::mutation_version`. An insertion, deletion, reorder, label retarget, switch-target edit or exception table edit must invalidate it. Exception-table mutation version may be tracked at `CodeAttribute` level if separate from `MethodCode`.

## Verification/test cases

- Single block, unconditional and conditional branches, nested loops, back edges, fallthrough, unreachable trailing bytes.
- Table/lookup switches with shared and distinct targets, self-loops, switch as last instruction.
- One handler, multiple overlapping/partially overlapping handlers, shared handler targets, catch-all and typed handlers, handler ranges ending at `code_length`.
- Handler protecting a constructor invocation: local state before `invokespecial` must feed the exceptional edge.
- Invalid branch/switch targets, zero-length/invalid handler ranges and reachable code falling off end must return structured errors.
- Rebuild after edits and show stale graphs cannot be used without detection.

## References

JVMS §4.7.3 (Code and exception tables), §4.9 (structural constraints), §4.10 (verification), and §6 (branch, switch, throw and subroutine opcodes). The graph is an **analysis view**, not canonical bytecode storage.
