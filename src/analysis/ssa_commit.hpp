/**
 * @file
 * @brief Atomic installation of a lowered method and its class-level dependencies.
 * @ingroup implementation
 */
#pragma once

#include <cafe/analysis/context.hpp>
#include <cafe/analysis/frame.hpp>
#include <cafe/class_file.hpp>
#include <cafe/error.hpp>

namespace cafe::detail {

class SymbolicEdit;
struct SsaCodeProvenance;

/// Inputs whose mutation invalidates a detached SSA graph.
struct SsaSourceStamp {
  std::uint64_t declaration_revision{}; ///< Globally scoped owner declaration stamp.
  std::uint64_t hierarchy_snapshot{}; ///< Identity of the analysis hierarchy.
  std::uint64_t code_revision{}; ///< Instruction or label revision at graph creation.
  std::uint64_t metadata_revision{}; ///< Last handler or metadata replacement.
  std::uint32_t code_scope{}; ///< Stable method instruction scope, zero for a declaration without Code.
  friend bool operator==(const SsaSourceStamp&, const SsaSourceStamp&) = default;
};

/// Owns unpublished instructions and an RAII checkpoint of appended constants/bootstrap rows.
class SsaCommit {
public:
  /// Start a transaction for a method in the exclusively accessed class.
  /// @param owner Class receiving a verified method body.
  /// @param method_index Index of its declaration.
  SsaCommit(ClassFile& owner, std::size_t method_index);
  /// Roll back every unpublished class-level edit.
  ~SsaCommit();
  /// Each transaction owns one rollback checkpoint.
  SsaCommit(const SsaCommit&) = delete;
  /// A live checkpoint cannot be reassigned.
  SsaCommit& operator=(const SsaCommit&) = delete;

  /// Capture the owner, method, metadata and verifier context identities.
  /// @param owner Class owning the declaration.
  /// @param method_index Index of the method within the class.
  /// @param context Hierarchy used for verification.
  /// @return Revision stamp, or a zero stamp for an invalid index.
  static SsaSourceStamp source_stamp(const ClassFile& owner, std::size_t method_index,
                                     const analysis::AnalysisContext& context) noexcept;
  /// Read live class storage charged by staged instructions and transactional constant resolution.
  /// @return The class resource's current charged byte count.
  [[nodiscard]] std::size_t class_allocation_bytes() const noexcept;

  /// The detached body receiving newly allocated labels, instructions and switches.
  /// @return Reference valid until publication or destruction.
  MethodCode& code() noexcept {
    return staged;
  }
  /// Resolve symbolic operands using the class editor's transactional resolver.
  /// @param value Borrowed symbolic or concrete instruction.
  /// @return A builder using class-owned constants, or an operand/capacity error.
  std::expected<InstructionBuilder, CodeEditError> resolve(const MethodInstruction& value);
  /// Resolve a loadable constant without changing its primitive payload bits.
  /// @param value Primitive, class, string, handle, method type, or dynamic constant.
  /// @return A class-owned constant index or an operand/capacity error.
  std::expected<CpIndex, CodeEditError> resolve(const BootstrapArgument& value);
  /// Verify, relocate and install the complete body after checking its source snapshot.
  /// @param stamp Snapshot captured when the SSA graph was created.
  /// @param context Hierarchy and resource limits used to verify the final layout.
  /// @param handlers Emitted exception clauses in catch precedence order.
  /// @param seeds Disconnected verifier roots keyed by staged instruction identity.
  /// @param provenance Instruction, slot and handler mappings for metadata relocation.
  /// @param drop_unsafe Whether unrelocatable metadata may be dropped with a warning.
  /// @return Drop diagnostics or the failure that prevents atomic publication.
  std::expected<std::vector<Diagnostic>, WriteError> finish(
      const SsaSourceStamp& stamp, analysis::AnalysisContext& context,
      std::span<const ExceptionHandler> handlers, std::span<const analysis::StackMapFrame> seeds,
      const SsaCodeProvenance& provenance, bool drop_unsafe);

private:
  /// Commit the symbolic edit checkpoint, after publishing the verified Code.
  void accept_constants() noexcept;
  /// @brief Intern an attribute name within the rollback checkpoint.
  /// @param value Modified UTF-8 attribute name.
  /// @return Its class-pool index, or a pool capacity/encoding failure.
  std::expected<CpIndex, CodeEditError> utf8(std::string_view value);
  ClassFile& owner; ///< Exclusively accessed destination class.
  std::size_t method_index; ///< Index of the method receiving the emitted instructions.
  MethodCode staged; ///< Fresh instructions allocated using the class's storage resource.
  std::unique_ptr<SymbolicEdit> transaction; ///< Pool and bootstrap rollback checkpoint.
};

} // namespace cafe::detail
