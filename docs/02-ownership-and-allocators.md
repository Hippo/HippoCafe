# 02 — Ownership, Allocators, and Memory Lifetime

## Goals

Keep peak memory and allocation count low while allowing a movable, independently owned `ClassFile`, stable instruction handles, and safe recovery from partially malformed input. Allocation strategies follow **lifetime**, not merely object type. Memory resources do not run destructors for constructed objects; RAII owners must do so.

## Ownership tree

```text
ReadResult
  ├─ ClassFile
  │    ├─ ClassStorage (stable heap address, resource objects)
  │    │    ├─ metadata_arena: monotonic_buffer_resource
  │    │    └─ mutable_pool: unsynchronized_pool_resource
  │    ├─ owned original byte buffer (optional empty for programmatic classes)
  │    ├─ ConstantPool (index-stable, class lifetime)
  │    ├─ fields / methods / attributes / raw-source slices
  │    └─ MethodCode(s)
  │         ├─ fixed-width instruction slots + ordering vector
  │         ├─ label table
  │         └─ switch operand allocations
  └─ diagnostics (owned; independent strings)

Writer
  └─ per-write scratch arena (temporary; discarded after writing)
```

A `ClassFile` must be safely usable after the caller frees the original input span. The reader should preferably accept a moved owned byte vector and hand ownership to the class. The span overload may copy into a class-owned blob once. Represent unmodified attribute bodies and original Code bodies as `SourceSlice { offset, length }` into this owned blob, not as dangling external `span`s.

## C++23 resource policy

```cpp
struct ClassStorage {
    explicit ClassStorage(std::pmr::memory_resource* upstream)
        : metadata_arena(upstream),
          mutable_pool(upstream) {}

    std::pmr::monotonic_buffer_resource metadata_arena;
    std::pmr::unsynchronized_pool_resource mutable_pool;
};

class ClassFile {
    std::unique_ptr<ClassStorage> storage; // declared before resource users
    ConstantPool constant_pool;
    std::pmr::vector<Field> fields;
    std::pmr::vector<Method> methods;
    std::pmr::vector<Attribute> attributes;
    // Own original bytes or shared immutable backing buffer.
};
```

This is a **shape**, not copy/paste-ready code: member initialization and destruction order must be implemented correctly. The `storage` owner is constructed first and destroyed last. Every resource-backed object must be destroyed before it. `ClassStorage` may place the mutable pool upstream of the metadata arena, but defaulting both to the supplied upstream resource simplifies reclamation accounting and avoids surprising pool retention until entire class destruction.

### Allocation domains

| Lifetime / data | Recommended resource | Why |
|---|---|---|
| Class lifetime: interned strings, mostly immutable metadata | `monotonic_buffer_resource` | Extremely cheap allocation; dispose entire class at once |
| Method code: vectors, labels, variable-size switch payloads | `unsynchronized_pool_resource` or normal PMR resource | Reuse freed allocations and vector growth buffers |
| Writer pass: constant mapping, offsets, encoded buffer | scoped `monotonic_buffer_resource` for scratch, regular growing output buffer | Predictable temporary cleanup |
| Reader pass: parse scratch tables, offset mappings | scoped arena / local vectors | Never retain scratch in class |
| Error and diagnostic output | ordinary owned values | Must outlive temporary parser arena |

**Important:** a `std::pmr::vector` that grows on a monotonic resource cannot reclaim its previous backing buffer before the entire resource dies. Reserve known counts after validating them against input length/budgets; prefer the recycling pool for growing/mutable vectors.

## Class movement and cloning

`std::pmr::*_resource` objects themselves are not movable. Therefore:

- `ClassFile` owns a heap-stable allocation context (`unique_ptr<ClassStorage>`).
- Permit move construction only when all moved PMR objects continue using the same resource and ownership/order remain valid. Implement a custom move constructor if default behavior is not demonstrably correct.
- **Do not** casually default move assignment: PMR container allocator propagation rules can cause unexpected elementwise moves and resource mismatch. Delete move assignment initially, or implement it explicitly and test it.
- Delete copy operations. Provide explicit `ClassFile clone(const ClassOptions&) const` later when every copied raw-source reference and index mapping is handled correctly.
- Each class's mutable resource is independent; separate classes can be processed on separate threads. The mutable pool is not thread-safe within one class.
- An optional injected `upstream_resource` must outlive the class. Document that contract, or wrap ownership explicitly when exposing it.

## Instruction slot storage

Use stable logical IDs, not public persistent pointers:

```cpp
struct InstructionId {
    uint32_t index;
    uint32_t generation;
};

struct InstructionSlot {
    Instruction instruction;   // 8 bytes
    uint32_t generation;
    uint32_t next_free;
    bool occupied;
    // Arrange fields to minimize padding; measure sizeof.
};

struct MethodStorage {
    std::pmr::vector<InstructionSlot> slots;
    std::pmr::vector<uint32_t> order;
    uint32_t free_head = UINT32_MAX;
};
```

Slots can relocate with vector growth; returned `InstructionId`s remain valid. Using a slot-map index as an instruction ID is safe only when generation is checked. No reference to `slots[i].instruction` survives an operation that can grow the vector. The public `get()` operation returns a temporary reference; document invalidation clearly.

The freelist recycles slots on removal. When removing a live instruction: unlink its index from `order`, destroy owned out-of-line payloads, increment generation (define safe wraparound policy), mark slot free, link it to `free_head`. A newly allocated slot is initialized completely. A newly reused slot never inherits stale flags/payload handles.

For pass one, optimize for a good predictable baseline: **slot vector + order vector**, not a custom slab allocator. A dense `order` array provides fast repeated walks; middle insertion is linear in method size but involves only moving 32-bit IDs. Benchmark against indexed links before changing it.

## Variable-size operands

Use method-local tables with stable payload IDs:

- `TableSwitch`: signed low key, default `LabelId`, PMR vector of target labels.
- `LookupSwitch`: default `LabelId`, PMR vector of sorted `(int32_t key, LabelId)` pairs.
- Other future variable-length operands must use the same out-of-line pattern, not enlarge `Instruction`.
- Switch payload IDs may be reused only after invalidating or generation-protecting references, and each payload has exactly one owner unless an explicitly shared immutable payload model is introduced.

A `std::variant<TableSwitch, LookupSwitch>` is reasonable **in the small switch table**, never inside each instruction.

## Class constant pool / byte ownership

Preserve original CP slot indices, including duplicated entries and reserved slots following `Long`/`Double`. For parsed data, keep raw modified-UTF-8 bytes and exact float/double bit representations. Prefer arena-backed strings/byte arrays and decode names lazily, caching only on demand. Interning is an optimization when appending *new* values; it must not discard original duplicates.

For original payloads, prefer offset+size slices into the owned input buffer. Check slice bounds on construction. A raw attribute created programmatically stores its own owned bytes. Avoid copying every raw attribute on parsing. If you introduce a source-backed slice, the writer should not assume byte offsets remain meaningful after code modifications.

## Budgets / untrusted input

The parser must never reserve memory solely because an attacker supplied a large count. Use checked arithmetic and limits for total input size, total decoded allocations, attributes, annotation nesting, switch cases and code size. Before each reserve, validate `count * minimum_element_size <= remaining_payload` where relevant. Use an optional budget-tracking upstream resource if needed; return `ResourceLimitExceeded` rather than attempting multi-gigabyte allocation.

Do not use a process-wide `std::pmr::set_default_resource()`; do not return PMR containers allocated from short-lived parser scratch into a long-lived result. Parsing failed attributes should not leave partially mutated persistent class objects or leaked allocations in reusable pools. Decode uncertain attributes in a short-lived scratch resource; construct committed persistent objects only after validation. Allocating speculative attribute contents directly in the class monotonic arena and then dropping them can retain attacker-controlled transient allocations for the class lifetime.

## Testing allocation correctness

- ASan/UBSan: malformed input, slot reuse, class destruction, moved class, source-buffer lifetime.
- Generation tests: stale ID always rejected after erase/reuse.
- Allocation counting: low allocation counts per instruction; switch allocations scale with number/size of switches, not total instructions.
- Peak memory and RSS on large synthetic classes; compare 8-byte instruction storage to physical slot and order overhead.
- No unexpected allocation per fixed-width no-operand instruction.
- `ClassFile` destruction is correct for nontrivial PMR strings/vectors and originally parsed raw source slices.
