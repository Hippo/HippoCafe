/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup implementation
 */

#pragma once

#include <expected>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <cafe/analysis/analysis_error.hpp>
#include <cafe/analysis/frame.hpp>
#include <cafe/bytecode/instruction.hpp>
#include <cafe/class_file.hpp>

namespace cafe::classpath {
class ClassHierarchy;
}

namespace cafe::analysis {

/**
 * @brief Per-method JVM instruction simulator and prepared constant-effect cache. Owner and
 * scratch storage must outlive it; initialize the entry frame and prepare all instructions before
 * applying effects.
 * @ingroup implementation
 *
 * One simulator per method. A null hierarchy selects the hierarchy-independent
 * stack analysis. The owner and scratch resource must outlive the simulator.
 * Initialize the entry frame before preparing and applying instructions.
 */
class InstructionEffects {
public:
  /**
   * @brief Initialize per-method JVM instruction simulator and prepared constant-effect cache
   * @param owner Class owning the method, constants, or source slices being inspected.
   * @param method Method belonging to the supplied class.
   * @param hierarchy Reference-resolution hierarchy; a null internal pointer selects category-only
   *   analysis.
   * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
   */
  InstructionEffects(const ClassFile& owner, const Method& method, const classpath::ClassHierarchy* hierarchy,
                     std::pmr::memory_resource* scratch);

  /**
   * @brief Validate method identity/signature and create its entry locals, stack, and constructor
   * state.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @return Success or an invalid declaration/descriptor/local-limit error.
   */
  [[nodiscard]] std::expected<void, AnalysisError> initialize(Frame& frame, std::uint16_t max_locals);
  /**
   * @brief Validate static operands in code order and cache descriptor-dependent effects before
   * propagation.
   * @param at Stable instruction handle belonging to the supplied method.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @return Success or a version/operand/reference/subroutine error.
   *
   * Validate static operands in code order, including those in unreachable
   * blocks, and cache descriptor-dependent effects before the worklist runs.
   */
  [[nodiscard]] std::expected<void, AnalysisError> prepare(InstructionId at, Instruction instruction,
                                                           std::uint16_t max_locals);
  /**
   * @brief Apply a prepared instruction to a normal successor frame and identify any newly
   * initialized receiver.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param at Stable instruction handle belonging to the supplied method.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   * @return The pre-initialization receiver when a constructor succeeds, an empty optional
   *   otherwise, or an analysis error.
   *
   * Mutates the normal successor state; a successful constructor call returns
   * the pre-instruction receiver identity for the exceptional transfer.
   */
  [[nodiscard]] std::expected<std::optional<VerificationType>, AnalysisError>
  apply(Frame& frame, InstructionId at, Instruction instruction);

private:
  /**
   * @brief Allocator-owned invocation parameter types, result type, and parameter slot count cached
   * by descriptor.
   * @ingroup implementation
   */
  struct InvocationShape {
    /**
     * @brief Initialize allocator-owned invocation parameter types, result type, and parameter slot
     * count cached by descriptor.
     * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
     */
    explicit InvocationShape(std::pmr::memory_resource* scratch) : arguments(scratch) {
    }

    std::pmr::vector<VerificationType> arguments; ///< Owned parameter verification types in invocation order.
    VerificationType result; ///< Invocation result verification type; Top represents void.
    unsigned parameter_slots = 0; ///< Invocation parameter slot count, excluding an implicit receiver.
  };

  /**
   * @brief Prepared descriptor-dependent instruction behavior, including member identity and
   * constructor/access constraints.
   * @ingroup implementation
   */
  struct ConstantEffect {
    VerificationType value; ///< Prepared constant, field, or invocation-result verification type.
    std::string_view member_class; ///< Borrowed internal name of the referenced member's symbolic owner.
    std::string_view member_name; ///< Borrowed JVM name of the referenced member.
    std::string_view member_descriptor; ///< Borrowed JVM descriptor of the referenced member.
    std::span<const VerificationType> arguments; ///< Prepared invocation parameter verification types.
    /**
     * @brief Expected invokeinterface slot count or maximum multianewarray dimension count.
     *
     * Exact invokeinterface count or maximum multianewarray dimensions.
     */
    unsigned auxiliary_limit = 0;
    bool constructor = false; ///< Whether this effect invokes an instance initializer.
    bool declared_here = false; ///< Whether the referenced field/method is declared directly by the analyzed owner.
    std::optional<bool> protected_receiver{}; ///< Cached decision about protected cross-package receiver restrictions.
  };

  /**
   * @brief Successful verification step or a structured AnalysisError.
   */
  using Status = std::expected<void, AnalysisError>;
  /**
   * @brief Verification value or a structured AnalysisError.
   */
  using ValueResult = std::expected<VerificationType, AnalysisError>;

  /**
   * @brief Resolve a validated constant operand into its cached type/member/invocation behavior.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   * @return An owned prepared effect, or a reference/descriptor/access error.
   */
  [[nodiscard]] std::expected<ConstantEffect, AnalysisError> read_constant(Instruction instruction);
  /**
   * @brief Apply a prepared instruction's stack/local/type transition.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param at Stable instruction handle belonging to the supplied method.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   * @param initialized Output receiver identity successfully initialized by this instruction, when
   *   applicable.
   * @return Success or a verification failure; initialized identifies constructor effects for
   *   exceptional edges.
   */
  [[nodiscard]] Status execute(Frame& frame, InstructionId at, Instruction instruction,
                               std::optional<VerificationType>& initialized);
  /**
   * @brief Apply a constant-bearing instruction using its prepared effect.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param at Stable instruction handle belonging to the supplied method.
   * @param instruction Logical instruction value; scoped operands require the owning method.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @param initialized Output receiver identity successfully initialized by this instruction, when
   *   applicable.
   * @return Success or a verification/type/access failure.
   */
  [[nodiscard]] Status execute_constant(Frame& frame, InstructionId at, Instruction instruction,
                                        ConstantEffect& effect, std::optional<VerificationType>& initialized);
  /**
   * @brief Check field access and update operand-stack state.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @return Success or a receiver/value/access verification error.
   */
  [[nodiscard]] Status execute_field(Frame& frame, Opcode opcode, ConstantEffect& effect);
  /**
   * @brief Pop invocation arguments and receiver, enforce constructor/access rules, and push any
   * result.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @param initialized Output receiver identity successfully initialized by this instruction, when
   *   applicable.
   * @return Success or an invocation verification error.
   */
  [[nodiscard]] Status execute_invoke(Frame& frame, Opcode opcode, ConstantEffect& effect,
                                      std::optional<VerificationType>& initialized);
  /**
   * @brief Validate a constructor receiver and replace all normal-path aliases by its initialized
   * type.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @param initialized Output receiver identity successfully initialized by this instruction, when
   *   applicable.
   * @return Success or an invalid constructor-state error; returns the old identity through
   *   initialized.
   */
  [[nodiscard]] Status initialize_receiver(Frame& frame, ConstantEffect& effect,
                                           std::optional<VerificationType>& initialized);
  /**
   * @brief Determine whether protected member access requires a receiver subtype restriction.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @param method True selects method resolution; false selects field resolution.
   * @return True when the extra restriction applies, or a resolution error.
   */
  [[nodiscard]] std::expected<bool, AnalysisError> requires_protected_receiver(const ConstantEffect& effect, bool method);
  /**
   * @brief Check the receiver subtype required by protected cross-package access.
   * @param receiver Actual instance receiver verification type.
   * @param effect Prepared constant/member/invocation behavior for the instruction.
   * @param method True selects method resolution; false selects field resolution.
   * @return Success or a receiver/resolution error.
   */
  [[nodiscard]] Status check_protected_receiver(VerificationType receiver, ConstantEffect& effect, bool method);
  /**
   * @brief Enforce invokespecial owner restrictions for non-constructor calls.
   * @param tag Required constant-pool tag.
   * @param target Method-scoped branch destination.
   * @return Success or an invalid owner/reference/hierarchy error.
   */
  [[nodiscard]] Status validate_special_owner(CpTag tag, std::string_view target);
  /**
   * @brief Check array/index/value categories for array loads and stores.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @return Success or a stack/type error; aastore's concrete component check remains a JVM runtime
   *   responsibility.
   */
  [[nodiscard]] Status execute_array_access(Frame& frame, Opcode opcode);
  /**
   * @brief Check the method return category and constructor initialization state.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @return Success or a return/type/constructor error; remaining operand values are discarded.
   */
  [[nodiscard]] Status execute_return(Frame& frame, Opcode opcode);
  /**
   * @brief Check one verification value against an expected category/reference type.
   * @param value Actual verification type to check against the required type.
   * @param expected Required verification type or selected declaration identity.
   * @param allow_uninitialized Whether an uninitialized reference is permitted by this specific
   *   operation.
   * @return Success or a category/uninitialized/assignability/resolution error.
   */
  [[nodiscard]] Status match(VerificationType value, VerificationType expected,
                             bool allow_uninitialized = false);
  /**
   * @brief Pop one operand and check it against an expected verification type.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param expected Required verification type or selected declaration identity.
   * @param allow_uninitialized Whether an uninitialized reference is permitted by this specific
   *   operation.
   * @return The checked value, or an underflow/type error.
   */
  [[nodiscard]] ValueResult pop_matching(Frame& frame, VerificationType expected, bool allow_uninitialized = false);
  /**
   * @brief Apply a fixed sequence of expected pops and an optional push.
   * @param frame Mutable verifier locals, operand values, and constructor state.
   * @param popped Expected top-first operand types to remove.
   * @param pushed Optional verification value to append after successful pops.
   * @return Success or a stack/type failure.
   */
  [[nodiscard]] Status transition(Frame& frame, std::initializer_list<VerificationType> popped,
                                  std::optional<VerificationType> pushed = std::nullopt);
  /**
   * @brief Intern the array type produced by allocating reference elements.
   * @param component Reference component internal name or array descriptor.
   * @return A stable scratch-owned descriptor view.
   */
  [[nodiscard]] std::string_view array_of(std::string_view component);

  const ClassFile& owner; ///< Borrowed class owning the analyzed method and constant pool.
  const Method& method; ///< Borrowed owning method declaration.
  const classpath::ClassHierarchy* hierarchy; ///< Optional borrowed reference hierarchy; null selects category-only checking.
  std::pmr::memory_resource* scratch; ///< Borrowed temporary allocator for work buffers and interned names.
  std::string_view self; ///< Borrowed internal name of the analyzed class.
  std::string_view superclass; ///< Borrowed internal name of the direct superclass, if any.
  VerificationType return_type; ///< Verification type required by this method's return descriptor.
  std::pmr::unordered_set<std::pmr::string> array_names; ///< Stable scratch-owned array descriptor intern set.
  std::pmr::unordered_map<std::uint16_t, InvocationShape> method_descriptors; ///< Cached invocation shapes keyed by descriptor constant index.
  /**
   * @brief Prepared constant-dependent effects keyed by instruction slot.
   */
  std::pmr::unordered_map<std::uint32_t, ConstantEffect> constants;
  std::optional<std::pmr::vector<std::shared_ptr<const classpath::ClassInfo>>> superclass_chain; ///< Pinned validated superclass metadata retained for member/access checks.
};

} // namespace cafe::analysis
