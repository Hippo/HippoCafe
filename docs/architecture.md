# Architecture

The installed `hippocafe::hippocafe` target exposes headers under `cafe/` and the
convenience header `<hippo/cafe.hpp>`. Implementation headers under `src/` are private.
The API uses PascalCase types and enum values, snake_case functions and members,
and uppercase compile-time constants. JVM wire names retain their specified spelling.

## Modules and data flow

| Module | Responsibility |
|---|---|
| Model | Owned class declarations, ordered attributes, constant-pool indices, and original source bytes |
| Bytecode | Compact logical instructions, method-scoped handles, labels, and switch operands |
| I/O | Bounded parsing, format validation, recovery, encoding selection, and serialization |
| Archives | Validated ZIP/JMOD indexing, retained payload access, staged edits, and publication |
| Classpath | Explicit class-source registration, metadata-only scanning, and hierarchy caches |
| Analysis | CFG construction, verifier states, optional editable SSA, and method lowering |

Reading produces an owning class and diagnostics. Editing changes that model through
its APIs. Writing validates the model, chooses preservation or computed metadata,
and returns an independent output buffer. Writer analysis uses temporary emission
plans rather than changing canonical instructions or appending frame constants to
the original pool. Classpath resolution shares the archive backend and scans only
metadata needed for the requested hierarchy or member query.

## Ownership and allocation

`ClassFile` owns stable heap storage, an immutable original byte vector, a budget
resource, an arena for immutable metadata, and allocator-bound mutable tables.
Declaration order ensures the resources outlive their containers. Move construction
transfers ownership without relocating that storage; assignment is deliberately
restricted for allocator-bound model objects.

Constant-pool indices remain stable between model compactions. Long and double constants occupy two slots.
Numeric payloads preserve their exact bits. Original modified UTF-8 views borrow the
source; programmatic UTF-8 appends use separately allocated stable buffers, including
short strings. Entry pointers can be invalidated by pool growth even though string
views and indices remain stable across appends. Model compaction stages a replacement
pool and updated references before publishing either change.

Mutable buffers allocate directly through the budget resource, so failed speculative
decodes and discarded payloads release their charged storage. The parser translates
its own budget exception into a structured limit error and disarms that read-time
budget after successful parsing. General upstream allocation failure may still throw
`std::bad_alloc`; returning `std::expected` does not promise an exception-free API.

Returned spans, pointers, and string views have specific owners. Adding or erasing
members invalidates the corresponding declaration views and borrowed editors.
Instruction ranges and switch views must be reacquired after code edits. Source-byte
views remain valid until class storage destruction. A caller-supplied PMR resource
must outlive every object or analysis result that uses it.

## Instructions and edit transactions

`Instruction` is an eight-byte trivially copyable value. Logical opcodes normalize
implicit local operations and wide encodings; encoding hints retain useful original
wire choices. Branches and switches use scoped labels while their compact stored
operands refer to method-owned tables.

Instruction slots carry generations, and a separate order vector defines traversal.
Slot reuse invalidates stale handles. Bound labels identify positions before
instructions or at the exclusive method end. Labels at a shared boundary have
an order, so an insertion can separate them. Unbound labels support forward branches.
Switch payloads live out of line. Generation/revision exhaustion is reported rather
than allowing stale handles to become valid again.

An instruction view pairs the copied instruction and stable ID with its class
storage, so typed accessors resolve the encoded member on demand. Pattern
searches traverse these views in method order. Matches retain their original
instruction IDs and capture contents. Range edits check the method scope,
current contiguity, and whether any selected instruction or its branch or switch
target bindings have changed since the search.

`MethodEditor` resolves each sequence under one constant-pool and bootstrap
checkpoint. The method stages instruction slots, switch payloads, label fixups,
and order changes before publishing. A failed edit preserves logical contents.
A successful sequence edit advances the revision once. Inserting between nodes
sends the preceding labels to the patch and the following labels to the original
instruction. The compatible bare-instruction `insert_before` inserts ahead of
that instruction's labels. `insert_before_and_redirect_branches` rewrites existing
jumps and switch operands to use a new entry label. `insert_at` rebinds its selected
label. Erasure requires every removed instruction to be free of bound labels.

If a symbolic instruction or exception-handler edit runs out of pool slots, the
retry uses a compacted pool with any pending raw operands marked as additional roots.
The original pool stays available to borrowed strings until the edit succeeds.
A failed retry restores the indices and payloads from the original model.

## Parsing, preservation, and emission

Binary readers use bounded spans and explicit big-endian operations. Attribute
lengths create subreaders before decoding. The format registry supplies version and
location rules; codecs do not attempt to interpret version-inapplicable bodies.
Optional malformed data can be dropped in compatible mode, while essential failures
remain fatal unless a fully framed Code body qualifies for explicit opaque recovery.

The writer allocates a separate constant pool from the references in emitted
instructions, declarations, and attributes. It assigns slots to narrow `ldc`
operands first so relinking keeps the chosen bytecode offsets. It keeps independent
JVM symbolic references in separate slots and follows reachable bootstrap arguments.
Selecting index preservation allows unchanged Code to use its original bytes.

Re-encoding validates operands, chooses local and load widths, resolves labels, and
iterates branch layout to convergence. Computed frame emission can expand conditional
branches and add the resulting frame boundaries. Every offset-bearing attribute must
be safely remapped or explicitly dropped with a diagnostic.

Caller-supplied metadata is stamped with instruction revision and encoding policy.
A CFG additionally snapshots handler identity/order. Frame analysis can check the
class declaration revision and hierarchy snapshot. These checks prevent reuse after
inputs have changed; they are not synchronization primitives.

## Archives and classpaths

The shared archive reader validates the single-disk central directory and every
local record extent before delegating decompression to minizip-ng. JMOD input has a
validated wrapper; the ZIP stream uses the corresponding logical origin. Payload
reads enforce lengths, checksums, compression ratios, limits, and cancellation.

The editor keeps original entries and staged content separate. Unchanged entries can
copy compressed payloads while preserving supported metadata. A save completes a
temporary file before publishing it under the destination name. Existing opened
readers retain the original file after atomic replacement. Failed operations do not
publish partial output. These guarantees require opened source contents to remain
stable rather than being modified in place.

Classpaths resolve copied in-memory definitions, application sources, then runtime
sources in registration order. Builders produce independent immutable snapshots.
Resolver and hierarchy copies share synchronized caches; cached metadata is returned
through shared ownership. Class names use modified UTF-8; directory/JAR resource
names use ordinary UTF-8. The conversion checks surrogate and path constraints.

## Verification analysis

CFG blocks split at control-flow and exception-table boundaries. Normal edges retain
switch keys; protected ranges use a segment-tree cover to avoid eagerly duplicating
all exceptional edges. Dense instruction slots use a direct lookup table, with a
bounded fallback for sparse edited methods.

The frame engine validates instruction effects before worklist propagation. Stack
entries represent values; locals represent slots, including a continuation marker
for long/double. Exception edges use incoming locals, with special handling for a
constructor that fails before initializing its receiver.

Reference joins use the configured hierarchy. A common superclass is tried first;
ambiguous interface joins use bounded deterministic alternatives, with downstream
instructions rechecked against each candidate. Work and allocation budgets cover
retries. Converged frames are checked again before materialization and StackMapTable
encoding. The target JVM still performs linkage and loader/module access checks.

## Method-scoped SSA

Each SSA graph owns a snapshot of one method. Blocks contain typed definitions,
explicit phis and ordered operations. Edges identify individual control transfers.
An exceptional input takes its locals from the state immediately before the
protected operation, with the JVM exception as an edge-defined value.
A successful constructor call defines the initialized view of its receiver.
Uninitialized values retain their allocation-site identity.

The lifter reuses the verifier's instruction transfer rules to record operands,
results and stack permutations. Declared stack maps provide verifier inputs
for disconnected modern code. Method-entry parameters, declared root inputs,
and edge-defined exceptions have distinct definitions in the graph.

A read-only analysis snapshot indexes each value's operation operands, local
assignments, and incoming phi arguments. Dominance queries use the verifier's
program points for block entry, operand evaluation, normal instruction results,
and individual transfers. A result is available on normal completion of its
instruction. Each exceptional transfer sees the pre-instruction state and its
own caught value. Editing the graph invalidates the snapshot and its use spans.

Lowering resolves phi copies on individual transfers, assigns JVM local slots,
and maps protected instruction intervals to ordered catch clauses. Emission
stages the new instructions, class dependencies and relocated Code attributes.
It verifies final layout before publishing the method. A library-prepared body
can then be written under the default preservation policy. Subsequent bytecode
edits follow the same metadata invalidation rules as other code edits.
