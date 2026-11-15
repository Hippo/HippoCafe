/**
 * @file
 * @brief Control flow, JVM verification types, and derived method metadata.
 * @ingroup analysis
 */

#pragma once

#include <optional>
#include <string>

#include "cafe/classpath/class_info.hpp"
#include "cafe/ids.hpp"

namespace cafe::analysis {

/**
 * @brief Verifier, control-flow, resource, and class-resolution failures.
 * @ingroup analysis
 */
enum class AnalysisErrorKind {
  InvalidCode, ///< Method or instruction structure is invalid for analysis.
  MissingCode, ///< The operation requires decoded Code.
  InvalidDescriptor, ///< Field/method descriptor or parameter-slot count is invalid.
  InvalidConstantPoolReference, ///< Required constant reference or descriptor is invalid.
  InvalidLabelOrRange, ///< Branch label or exception-table range is invalid.
  UnsupportedClassVersion, ///< Declared version is outside the selected target or analysis profile.
  UnsupportedSubroutineAnalysis, ///< Legacy jsr/ret analysis is unsupported; preservation may still succeed.
  StackUnderflow, ///< Instruction consumes more values than the incoming stack contains.
  InvalidCategoryOperation, ///< Stack manipulation violates category-one/category-two grouping.
  IncompatibleJoin, ///< Incoming frame shapes cannot be merged.
  TypeMismatch, ///< Operand or local type does not satisfy the instruction contract.
  UninitializedUse, ///< Uninitialized object is used where initialization is required.
  InvalidConstructorState, ///< Constructor initialization rules are violated.
  TypeResolutionFailure, ///< Required class or member hierarchy information is unavailable.
  UnrepresentableFrame, ///< Verification state cannot be represented as a JVM stack-map frame.
  AmbiguousTypeJoin, ///< No bounded candidate reference join satisfies downstream uses.
  ResourceLimitExceeded, ///< Configured work, memory, count, or depth budget was exhausted.
  Cancelled, ///< The supplied stop token requested cancellation.
  UnreachableCode, ///< Unreachable modern code requires frames that cannot be safely inferred.
  StackOverflow, ///< Required stack slots exceed the class-file encoding limit.
  LocalOverflow, ///< Required local slots exceed the class-file encoding limit.
};

/**
 * @brief Structured analysis failure identifying the method, optional instruction or label, and
 * underlying resolution failure.
 * @ingroup analysis
 */
struct AnalysisError {
  AnalysisErrorKind kind = AnalysisErrorKind::InvalidCode; ///< Discriminator selecting the interpretation of this record.
  std::string class_name{}; ///< Internal name of the affected class.
  std::string method_name{}; ///< Name of the affected method.
  std::string descriptor{}; ///< JVM field or method descriptor.
  std::optional<InstructionId> instruction{}; ///< Instruction identity associated with this record.
  std::optional<LabelId> label{}; ///< Label associated with the failure, when known.
  std::string detail{}; ///< Owned human-readable explanation.
  std::optional<classpath::ResolutionError> resolution{}; ///< Underlying class-resolution failure, when applicable.
};

} // namespace cafe::analysis
