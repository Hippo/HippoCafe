/**
 * @file
 * @brief Per-method, editable static single assignment form for JVM bytecode.
 * @ingroup analysis
 */
#pragma once

#include <cafe/analysis/analysis_error.hpp>
#include <cafe/analysis/context.hpp>
#include <cafe/analysis/verification_type.hpp>
#include <cafe/bytecode/method_instruction.hpp>
#include <cafe/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cafe {
class ClassFile;
}

namespace cafe::analysis {

/**
 * @brief Identity valid for the lifetime of a definition in one SSA graph.
 * Erasure retires the slot permanently. Moving the graph preserves its handles.
 * @tparam Tag Distinguishes block, operation, value and edge handles at compile time.
 * @ingroup analysis
 */
template<typename Tag>
struct SsaId {
  std::uint32_t index = 0; ///< Zero-based slot in the owning graph.
  std::uint64_t scope = 0; ///< Nonzero graph generation.
  /// @return True for an initialized handle. Use the graph lookup to check liveness.
  explicit constexpr operator bool() const noexcept {
    return scope != 0;
  }
  friend constexpr bool operator==(SsaId, SsaId) = default;
};
using SsaBlockId = SsaId<struct SsaBlockTag>; ///< Graph-scoped basic block.
using SsaOperationId = SsaId<struct SsaOperationTag>; ///< Graph-scoped operation.
using SsaValueId = SsaId<struct SsaValueTag>; ///< Graph-scoped definition.
using SsaEdgeId = SsaId<struct SsaEdgeTag>; ///< Graph-scoped incoming transfer.

/// Verification type whose uninitialized identity refers to an SSA allocation operation.
struct SsaType {
  VerificationKind kind = VerificationKind::Top; ///< JVM verification category.
  std::string_view reference{}; ///< Owned by the graph after insertion.
  SsaOperationId allocation{}; ///< Defining new operation for uninitialized instances.
  /// @return Two JVM slots for long and double, one for other verification types.
  [[nodiscard]] constexpr unsigned width() const noexcept {
    return kind == VerificationKind::Long || kind == VerificationKind::Double ? 2 : 1;
  }
  friend constexpr bool operator==(SsaType, SsaType) = default;
};

/// Kind of definition producing an SSA value.
enum class SsaValueKind {
  Parameter, ///< Receiver or formal argument defined on method entry.
  Operation, ///< Value defined on normal completion of an operation.
  Phi, ///< Block-entry definition selected by incoming edge.
  Exception, ///< Caught exception supplied by one exceptional transfer.
  RootLocal, ///< Disconnected frame input originating in a local slot.
  RootStack, ///< Disconnected frame input originating in the operand stack.
};
/// Distinct branch arms, verifier roots and exceptional transfers.
enum class SsaEdgeKind {
  Entry, ///< Implicit method invocation entering the first block.
  DeadRoot, ///< Verifier-only predecessor of a disconnected region.
  Fallthrough, ///< Normal continuation or untaken conditional arm.
  Conditional, ///< Taken conditional branch.
  Unconditional, ///< Unconditional jump.
  SwitchCase, ///< Switch arm carrying a signed case key.
  SwitchDefault, ///< Default switch arm.
  Exceptional, ///< Transfer from a protected instruction to its handler.
};
/// One value available along a particular incoming edge.
struct SsaPhiInput {
  SsaEdgeId edge; ///< Incoming transfer selecting this definition.
  SsaValueId value; ///< Definition available along that edge.
};

/// A typed definition. Phi arguments and reference strings belong to the graph.
struct SsaValue {
  SsaValueId id{}; ///< Identity of this definition.
  SsaType type{}; ///< Verifier type seen by users of this definition.
  SsaValueKind kind{}; ///< Site that defines the value.
  SsaBlockId block{}; ///< Associated graph block.
  SsaOperationId operation{}; ///< Defining operation for an operation result.
  SsaEdgeId edge{}; ///< Defining entry, verifier-root, or exceptional transfer when applicable.
  std::uint32_t slot = 0; ///< Incoming JVM local slot or bottom-to-top operand-stack ordinal.
  std::pmr::vector<SsaPhiInput> incoming; ///< Incoming edge/value pairs for a phi.
};
/// Original JVM local updated at this source position, retained for debugger-visible ranges.
struct SsaLocalAssignment {
  std::uint16_t slot; ///< Original JVM local index.
  SsaValueId value; ///< Value visible in the local after this operation.
};
/// An ordered JVM operation; branch targets and switch keys belong to edges.
struct SsaOperation {
  SsaOperationId id{}; ///< Graph identity of this operation.
  SsaBlockId block{}; ///< Block containing the operation.
  Opcode opcode = Opcode::Nop; ///< Logical JVM operation or provenance anchor.
  Instruction instruction{}; ///< Class-relative encoded operand for a concrete instruction.
  std::optional<MethodInstruction> symbolic{}; ///< Graph-owned symbolic member or dynamic operand.
  std::pmr::vector<SsaValueId> inputs; ///< Evaluation order, with the receiver before invocation arguments.
  SsaValueId result{}; ///< Normal result, including the promoted receiver of a constructor call.
  std::optional<InstructionId> origin{}; ///< Source instruction identity for lifted code.
  std::optional<SsaLocalAssignment> local_assignment{}; ///< Original local update used for debugger-visible variable ranges.
  bool anchor = false; ///< A source position whose stack or local movement was absorbed into SSA definitions.
};
/// One control-flow alternative. Exceptional inputs occur immediately before site.
struct SsaEdge {
  SsaEdgeId id{}; ///< Identity distinguishing this alternative even when targets coincide.
  SsaEdgeKind kind{}; ///< Normal, exceptional, or synthetic transfer category.
  SsaBlockId source{}; ///< Predecessor block for an instruction-originated transfer.
  SsaBlockId target{}; ///< Successor block.
  SsaOperationId site{}; ///< Protected operation for an exceptional edge.
  std::optional<std::int32_t> case_key{}; ///< Signed switch key for a case arm.
  std::optional<std::uint32_t> handler{}; ///< Index in the ordered handler table for an exceptional edge.
  SsaValueId exception{}; ///< Caught object defined on this exceptional transfer.
  bool constructor_uninitialized = false; ///< Constructor flag on a disconnected verifier root.
};
/// Ordered exception-table clause. Multiple clauses may share a handler block.
struct SsaHandler {
  SsaBlockId target; ///< Handler entry block.
  CpIndex catch_type; ///< Exception class, or zero for a catch-all clause.
  std::optional<std::uint16_t> origin{}; ///< Original exception-table ordinal for annotation relocation.
};
/// Ordered block. Its final operation represents its terminator.
struct SsaBlock {
  SsaBlockId id{}; ///< Identity of this block.
  std::pmr::vector<SsaValueId> phis; ///< Definitions evaluated simultaneously at entry.
  std::pmr::vector<SsaOperationId> operations; ///< Operations in execution order, including the terminator.
  std::pmr::vector<SsaEdgeId> incoming; ///< All incoming normal, exceptional, and verifier-root transfers.
  std::pmr::vector<SsaEdgeId> outgoing; ///< All transfers originating in this block.
};
/// A use of a value by an operation or phi, with the associated incoming edge for phis.
struct SsaUse {
  SsaOperationId operation{}; ///< Using operation when applicable.
  SsaValueId phi{}; ///< Using phi definition when applicable.
  SsaEdgeId edge{}; ///< Incoming phi edge when applicable.
  std::size_t index = 0; ///< Operand or incoming argument position.
  bool local_assignment = false; ///< This use maintains the debugger-visible local assignment.
};
/// Structured SSA failure class.
enum class SsaErrorKind {
  InvalidGraph, ///< The graph violates a structural or control-flow invariant.
  InvalidHandle, ///< A handle is stale, foreign, or unsuitable for the requested edit.
  StaleSource, ///< The bound method, metadata, declarations, or hierarchy changed.
  UnsupportedOpaqueCode, ///< The selected Code attribute has no decoded instruction stream.
  TypeMismatch, ///< A definition or operand violates its type contract.
  AnalysisFailure, ///< Bytecode analysis or type resolution failed.
  EmissionFailure, ///< The lowered body cannot be encoded or verified.
  UnsafeMetadata, ///< Nested Code metadata cannot be relocated safely.
  ResourceLimitExceeded, ///< A configured work, memory, or representation bound was exceeded.
  Cancelled, ///< The configured stop token requested cancellation.
};
/// Failure with an optional graph location and verifier cause.
struct SsaError {
  SsaErrorKind kind = SsaErrorKind::InvalidGraph; ///< Structured failure category.
  std::string detail{}; ///< Owned explanation.
  SsaBlockId block{}; ///< Associated block when known.
  SsaOperationId operation{}; ///< Associated operation when known.
  SsaEdgeId edge{}; ///< Associated transfer when known.
  std::optional<AnalysisError> analysis{}; ///< Underlying JVM verifier or resolution error when available.
  std::optional<WriteErrorKind> write_error_kind{}; ///< Writer failure category from final layout, verification, or metadata relocation.
};
/**
 * @brief Successful SSA operation or a structured failure.
 * @tparam T Type of the successful result, including void for updates.
 * @ingroup analysis
 */
template<typename T>
using SsaResult = std::expected<T, SsaError>;
/// Metadata relocation policy for lowering.
struct SsaLowerOptions {
  bool drop_unsafe_code_attributes = false; ///< Permit dropping unrelocatable metadata with diagnostics.
};
/// Recoveries selected by the caller while installing the rewritten body.
struct SsaLowerResult {
  std::vector<Diagnostic> diagnostics; ///< Metadata recoveries in emission order.
};

struct SsaStorage;
struct SsaAccess;

/// Owns a method SSA graph. Reacquire borrowed spans and pointers after an edit.
/// The analysis context's upstream resource must outlive the graph.
/// @ingroup analysis
class SsaMethod {
public:
  /// Release the graph and its owned values, instructions, and names.
  ~SsaMethod();
  /// Transfer the graph storage. Handles follow the new owner.
  /// @param other Source graph.
  SsaMethod(SsaMethod&& other) noexcept;
  /// Transfer graph storage and release any graph previously owned here.
  /// @param other Source graph.
  /// @return This graph after the transfer.
  SsaMethod& operator=(SsaMethod&& other) noexcept;
  /// A graph has a single owner and a unique handle scope.
  SsaMethod(const SsaMethod&) = delete;
  /// Graph identity cannot be duplicated by assignment.
  SsaMethod& operator=(const SsaMethod&) = delete;

  /**
   * @brief Look up the source declaration.
   * @return Method index captured when the graph was created.
   */
  [[nodiscard]] std::size_t method_index() const noexcept;
  /**
   * @brief Look up the method entry.
   * @return First executable block.
   */
  [[nodiscard]] SsaBlockId entry_block() const noexcept;
  /**
   * @brief Look up the implicit invocation edge.
   * @return Incoming edge carrying the receiver and parameters.
   */
  [[nodiscard]] SsaEdgeId entry_edge() const noexcept;
  /**
   * @brief Enumerate blocks in graph order.
   * @return Borrowed blocks, valid until the next graph edit.
   */
  [[nodiscard]] std::span<const SsaBlock> blocks() const noexcept;
  /**
   * @brief Inspect catch clauses in exception-selection order.
   * @return Borrowed ordered clauses.
   */
  [[nodiscard]] std::span<const SsaHandler> handlers() const noexcept;
  /**
   * @brief Inspect method entry values.
   * @return Receiver for instance methods, followed by descriptor arguments.
   */
  [[nodiscard]] std::span<const SsaValueId> parameters() const noexcept;
  /**
   * @brief Inspect a basic block.
   * @param id Graph-scoped handle to look up.
   * @return Borrowed block, or null for an invalid handle.
   */
  [[nodiscard]] const SsaBlock* block(SsaBlockId id) const noexcept;
  /**
   * @brief Inspect an instruction or source anchor.
   * @param id Graph-scoped handle to look up.
   * @return Borrowed operation, or null for an invalid handle.
   */
  [[nodiscard]] const SsaOperation* operation(SsaOperationId id) const noexcept;
  /**
   * @brief Inspect a control transfer.
   * @param id Graph-scoped handle to look up.
   * @return Borrowed edge, or null for an invalid handle.
   */
  [[nodiscard]] const SsaEdge* edge(SsaEdgeId id) const noexcept;
  /**
   * @brief Inspect a definition, including any phi inputs.
   * @param id Graph-scoped handle to look up.
   * @return Borrowed value, or null for an invalid handle.
   */
  [[nodiscard]] const SsaValue* value(SsaValueId id) const noexcept;
  /**
   * @brief Find uses of a definition in this graph.
   * @param id Graph-scoped handle to look up.
   * @return An independent list of operand, phi, and local-assignment uses, or an error.
   */
  [[nodiscard]] SsaResult<std::vector<SsaUse>> uses(SsaValueId id) const;

  /**
   * @brief Append an empty basic block.
   * @return Its graph-scoped handle, or a resource error.
   */
  [[nodiscard]] SsaResult<SsaBlockId> add_block();
  /**
   * @brief Define a typed phi at block entry. Fill its incoming edges with set_phi_input().
   * @param block Block belonging to this graph.
   * @param type JVM verification type. Reference names are copied into the graph.
   * @return The phi value, or a handle or resource error.
   */
  [[nodiscard]] SsaResult<SsaValueId> add_phi(SsaBlockId block, SsaType type);
  /**
   * @brief Insert or update the argument for one particular incoming transfer.
   * @param phi Phi defined in the destination block.
   * @param edge Transfer belonging to this graph.
   * @param value Definition available at this use.
   * @return Success or an invalid argument or resource error.
   */
  [[nodiscard]] SsaResult<void> set_phi_input(SsaValueId phi, SsaEdgeId edge, SsaValueId value);
  /**
   * @brief Add a normal control-flow arm, including a distinct edge for each switch key.
   * @param source Predecessor block.
   * @param target Destination block.
   * @param kind Transfer kind.
   * @param case_key Signed case key for a SwitchCase edge.
   * @return The new edge identity, or a handle, kind, or resource error.
   */
  [[nodiscard]] SsaResult<SsaEdgeId> add_edge(SsaBlockId source, SsaBlockId target, SsaEdgeKind kind,
                                              std::optional<std::int32_t> case_key = {});
  /**
   * @brief Append a clause at the end of the ordered handler table.
   * @param target Destination block.
   * @param catch_type Class constant for the caught type, or zero for catch-all.
   * @return Its index for subsequent exceptional transfers, or an error.
   */
  [[nodiscard]] SsaResult<std::uint32_t> add_handler(SsaBlockId target, CpIndex catch_type);
  /**
   * @brief Associate a protected program point with an ordered catch clause.
   * @param site Protected operation. Its result is unavailable on this edge.
   * @param handler Index of the applicable clause in handlers().
   * @param caught Type of the JVM exception supplied to the handler.
   * @return Edge defining the caught exception, or an error.
   */
  [[nodiscard]] SsaResult<SsaEdgeId> add_exception_edge(SsaOperationId site, std::uint32_t handler, SsaType caught);
  /**
   * @brief Give a disconnected block a verifier-only incoming transfer.
   * @param block Block belonging to this graph.
   * @param constructor_uninitialized Whether the entry frame carries the uninitialized-this flag.
   * @return Its root edge, or an error.
   */
  [[nodiscard]] SsaResult<SsaEdgeId> add_dead_root(SsaBlockId block, bool constructor_uninitialized = false);
  /**
   * @brief Declare a usable input from a disconnected verifier frame.
   * @param root Verifier-only edge returned by add_dead_root().
   * @param kind RootLocal or RootStack, selecting the input location.
   * @param slot Original local slot or bottom-to-top operand-stack ordinal.
   * @param type JVM verification type. Reference names are copied into the graph.
   * @return The incoming typed definition, or an error.
   */
  [[nodiscard]] SsaResult<SsaValueId> add_root_input(SsaEdgeId root, SsaValueKind kind, std::uint32_t slot, SsaType type);
  /**
   * @brief Append an operation with explicit value operands.
   * @param block Block belonging to this graph.
   * @param instruction Logical instruction with class-relative operands.
   * @param inputs Operand definitions in JVM evaluation order. Copied during this call.
   * @param result Type of the normal result, or empty for a resultless operation.
   * @return Operation handle. Inspect its result field for the defined value.
   */
  [[nodiscard]] SsaResult<SsaOperationId> append_operation(SsaBlockId block, Instruction instruction,
                                                           std::span<const SsaValueId> inputs = {}, std::optional<SsaType> result = {});
  /**
   * @brief Append a member access, invocation, or dynamic load from symbolic operands.
   * @param block Block belonging to this graph.
   * @param instruction Symbolic member or bootstrap operand. The graph copies its referenced data.
   * @param inputs Operand definitions in JVM evaluation order. Copied during this call.
   * @param result Type of the normal result, or empty for a resultless operation.
   * @return Operation handle, or an invalid operand or resource error.
   */
  [[nodiscard]] SsaResult<SsaOperationId> append_symbolic(SsaBlockId block, const MethodInstruction& instruction,
                                                          std::span<const SsaValueId> inputs = {}, std::optional<SsaType> result = {});
  /**
   * @brief Append the final control transfer of a block.
   * @param block Block belonging to this graph.
   * @param opcode Logical control-transfer opcode. Supply branch destinations with add_edge().
   * @param inputs Operand definitions in JVM evaluation order. Copied during this call.
   * @return Terminator handle, or an error.
   */
  [[nodiscard]] SsaResult<SsaOperationId> append_terminator(SsaBlockId block, Opcode opcode,
                                                            std::span<const SsaValueId> inputs = {});
  /**
   * @brief Insert an operation before an existing program point.
   * @param at Live operation immediately following the insertion point.
   * @param instruction Logical instruction with class-relative operands.
   * @param inputs Operand definitions in JVM evaluation order. Copied during this call.
   * @param result Type of the normal result, or empty for a resultless operation.
   * @return New operation identity, or an invalid handle or resource error.
   */
  [[nodiscard]] SsaResult<SsaOperationId> insert_before(SsaOperationId at, Instruction instruction,
                                                        std::span<const SsaValueId> inputs = {}, std::optional<SsaType> result = {});
  /**
   * @brief Rewrite the uses of a definition throughout the graph.
   * @param from Definition whose uses will change.
   * @param to Replacement definition.
   * @return Success or an invalid definition or resource error.
   */
  [[nodiscard]] SsaResult<void> replace_uses(SsaValueId from, SsaValueId to);
  /**
   * @brief Replace a single operand and retain its position in evaluation order.
   * @param op Operation whose operand will change.
   * @param index Zero-based position in its input list.
   * @param value Definition available at this use.
   * @return Success or an invalid argument or resource error.
   */
  [[nodiscard]] SsaResult<void> set_input(SsaOperationId op, std::size_t index, SsaValueId value);
  /**
   * @brief Move a normal edge to a new target. Supply arguments to the new target phis.
   * @param edge Transfer belonging to this graph.
   * @param target Destination block.
   * @return Success or an invalid edge or destination error.
   */
  [[nodiscard]] SsaResult<void> redirect_edge(SsaEdgeId edge, SsaBlockId target);
  /**
   * @brief Remove a transfer and its associated phi arguments.
   * @param edge Transfer belonging to this graph.
   * @return Success or an error for an invalid or indispensable transfer.
   */
  [[nodiscard]] SsaResult<void> erase_edge(SsaEdgeId edge);
  /**
   * @brief Remove an operation after disconnecting its result and exception edges.
   * @param operation Operation to remove.
   * @return Success or an invalid handle or remaining-use error.
   */
  [[nodiscard]] SsaResult<void> erase_operation(SsaOperationId operation);
  /**
   * @brief Remove an unused phi and its incoming arguments.
   * @param phi Phi defined in the destination block.
   * @return Success or an invalid handle or remaining-use error.
   */
  [[nodiscard]] SsaResult<void> erase_phi(SsaValueId phi);
  /**
   * @brief Check the graph against its source without dereferencing saved model views.
   * @param owner Class containing the bound method and constant pool.
   * @param context Hierarchy snapshot, resource limits, and cancellation token.
   * @return Whether the bound declaration, body, metadata, and hierarchy still match.
   */
  [[nodiscard]] bool is_current(const ClassFile& owner, const AnalysisContext& context) const noexcept;

  /**
   * @brief Inspect the values available from a method-entry or disconnected verifier root.
   * @param root Graph-scoped entry edge.
   * @return Owned parameter or root-input handles in definition order, or an invalid-handle/edge error.
   */
  [[nodiscard]] SsaResult<std::vector<SsaValueId>> root_inputs(SsaEdgeId root) const;

  /**
   * @brief Insert a symbolic operation, deep-copying its operand and bootstrap graph.
   * @param at Existing operation before which to insert.
   * @param instruction Field, invocation, dynamic constant, or ordinary instruction.
   * @param inputs Graph values in evaluation order.
   * @param result Result verification type, or an empty optional for a void operation.
   * @return New operation handle or an edit error leaving the operation order intact.
   */
  [[nodiscard]] SsaResult<SsaOperationId> insert_symbolic_before(SsaOperationId at,
                                                                 const MethodInstruction& instruction, std::span<const SsaValueId> inputs = {}, std::optional<SsaType> result = {});

private:
  friend struct SsaAccess;
  /// Adopt freshly allocated graph storage.
  /// @param storage Owned graph and allocator.
  explicit SsaMethod(std::unique_ptr<SsaStorage> storage) noexcept;
  std::unique_ptr<SsaStorage> storage; ///< Stable graph and allocator ownership.
};

/**
 * @brief Render the editable graph in block and instruction order. Includes typed definitions,
 * edge-labeled phi arguments, and the protected operation and handler for each exceptional edge.
 * IDs use graph-local indices so independently built copies of the same graph can be compared.
 * @param graph Method graph to inspect, which may be only partially constructed.
 * @return Diagnostic text for people reading or comparing graphs.
 * @ingroup analysis
 */
[[nodiscard]] std::string dump_ssa(const SsaMethod& graph);

struct SsaAnalysisData;

/// Read-only indexes for one verified graph revision. Moving the graph preserves the snapshot.
/// A successful graph edit or destruction invalidates the snapshot and all borrowed use spans.
/// @ingroup analysis
class SsaAnalysis {
public:
  /// @return Whether the graph still owns this revision.
  [[nodiscard]] bool is_current() const noexcept;

  /// @brief Look up a definition's operation operands, debug assignments, and phi inputs.
  /// @param value Graph-scoped definition.
  /// @return Borrowed use sites, or InvalidHandle for a foreign handle or expired revision.
  [[nodiscard]] SsaResult<std::span<const SsaUse>> uses(SsaValueId value) const;

  /// @brief Test dominance of block entries. A block dominates itself.
  /// @param definition Block whose entry is tested as the dominator.
  /// @param use Block being queried.
  /// @return Dominance result, or InvalidHandle for an invalid handle or expired revision.
  [[nodiscard]] SsaResult<bool> dominates(SsaBlockId definition, SsaBlockId use) const;

  /// @brief Test whether a definition dominates evaluation of the operation's operands.
  /// @param definition Parameter, phi, instruction result, or edge-defined value.
  /// @param use Operation at whose pre-state the definition must be available.
  /// @return Dominance result, or InvalidHandle for an invalid handle or expired revision.
  [[nodiscard]] SsaResult<bool> dominates(SsaValueId definition, SsaOperationId use) const;

  /// @brief Test whether a definition dominates a specific incoming phi transfer.
  /// @param definition Parameter, phi, instruction result, or edge-defined value.
  /// @param use Transfer, including an exceptional transfer from an operation's pre-state.
  /// @return Dominance result, or InvalidHandle for an invalid handle or expired revision.
  [[nodiscard]] SsaResult<bool> dominates(SsaValueId definition, SsaEdgeId use) const;

private:
  /** @cond duplicate_friend_declarations */
  friend SsaResult<SsaAnalysis> analyze_ssa(const ClassFile&, const SsaMethod&, AnalysisContext&);
  /** @endcond */
  /// Borrow the current graph-owned revision.
  /// @param indexes Ownership token for the verified indexes.
  explicit SsaAnalysis(const std::shared_ptr<SsaAnalysisData>& indexes) noexcept : indexes(indexes) {
  }
  std::weak_ptr<const SsaAnalysisData> indexes; ///< The graph owns the current revision's data.
};

/// @brief Verify a graph and expose its indexed uses and program-point dominance.
/// @param owner Class containing the bound method and constant pool.
/// @param graph Graph whose edits invalidate this snapshot.
/// @param context Hierarchy snapshot, resource limits, and cancellation token.
/// @return A snapshot of the current graph or a verification or resource error.
/// @ingroup analysis
[[nodiscard]] SsaResult<SsaAnalysis> analyze_ssa(const ClassFile& owner, const SsaMethod& graph, AnalysisContext& context);

/**
 * @brief Start an empty SSA body for a concrete method. Initializes the entry and its parameters.
 * @param owner Class containing the bound method and constant pool.
 * @param method_index Index of the concrete method in the current class declaration.
 * @param context Hierarchy snapshot, resource limits, and cancellation token.
 * @return An independently editable graph, or a binding, descriptor, or resource error.
 * @ingroup analysis
 */
[[nodiscard]] SsaResult<SsaMethod> create_ssa(const ClassFile& owner, std::size_t method_index, AnalysisContext& context);
/**
 * @brief Lift a decoded method into verified, typed SSA. The source class is borrowed for this call.
 * @param owner Class containing the bound method and constant pool.
 * @param method_index Index of the concrete method in the current class declaration.
 * @param context Hierarchy snapshot, resource limits, and cancellation token.
 * @return An owning graph, or a structured analysis, verification, or resource error.
 * @ingroup analysis
 */
[[nodiscard]] SsaResult<SsaMethod> lift_ssa(const ClassFile& owner, std::size_t method_index, AnalysisContext& context);
/**
 * @brief Validate graph structure, types, definitions and uses at normal and exceptional program points.
 * @param owner Class containing the bound method and constant pool.
 * @param graph Method graph bound to this class and analysis context.
 * @param context Hierarchy snapshot, resource limits, and cancellation token.
 * @return Success or a diagnostic locating the invalid operation, block, or edge when known.
 * @ingroup analysis
 */
[[nodiscard]] SsaResult<void> verify_ssa(const ClassFile& owner, const SsaMethod& graph, AnalysisContext& context);
/**
 * @brief Install ordinary bytecode and metadata for the bound method. The operation is transactional.
 * @param owner Class containing the bound method and constant pool.
 * @param graph Method graph bound to this class and analysis context.
 * @param context Hierarchy snapshot, resource limits, and cancellation token.
 * @param options Policy for nested Code attributes that cannot be relocated.
 * @return Recoveries on success, or an error preserving the prior class contents.
 * @ingroup analysis
 */
[[nodiscard]] SsaResult<SsaLowerResult> lower_ssa(ClassFile& owner, const SsaMethod& graph, AnalysisContext& context,
                                                  const SsaLowerOptions& options = {});
} // namespace cafe::analysis
