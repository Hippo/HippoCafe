/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstddef>
#include <string>

namespace cafe {

/**
 * @brief Importance of a recoverable diagnostic.
 * @ingroup model
 */
enum class Severity {
  Info, ///< Informational condition.
  Warning, ///< Recoverable issue requiring caller attention.
  Error, ///< Error-level diagnostic retained by a recovery path.
};
/**
 * @brief Action taken when preserving or recovering malformed content.
 * @ingroup model
 */
enum class RecoveryAction {
  None, ///< No recovery action or no operand, according to the enclosing enum.
  Dropped, ///< Affected content was omitted.
  PreservedOpaque, ///< Bounded undecodable essential content was retained opaquely.
  RetainedRaw, ///< Raw payload bytes were retained.
};
/**
 * @brief Machine-readable classification of recoverable read/write conditions.
 * @ingroup model
 */
enum class DiagnosticCode {
  DroppedMalformedAttribute, ///< A bounded malformed optional attribute was discarded.
  VersionInapplicable, ///< Attribute or encoding is not applicable to the declared version.
  InvalidAttributeContext, ///< Attribute is not permitted at this structural location.
  InvalidAttributeName, ///< Attribute name is not a valid UTF-8 constant reference.
  DuplicateAttribute, ///< A non-repeatable attribute occurs more than once.
  InvalidModifiedUtf8, ///< Bytes are not valid Java modified UTF-8.
  InvalidVersion, ///< Declared class-file version is invalid for this operation.
  TrailingBytes, ///< Bytes remain after the complete class-file envelope.
  OpaqueEssentialCode, ///< Essential Code was retained as an undecoded body.
  DiagnosticsSuppressed, ///< Further diagnostics were suppressed by the configured limit.
  DroppedUnsafeCodeAttribute, ///< An unrelocatable Code attribute was dropped during computed emission.
};

/**
 * @brief Recoverable condition with source location, explanation, and the action taken. Strings
 * are owned.
 * @ingroup model
 */
struct Diagnostic {
  Severity severity; ///< Importance of the reported recoverable condition.
  DiagnosticCode code; ///< Machine-readable recoverable diagnostic classification.
  std::size_t offset; ///< Absolute byte offset in the input class file, or zero when unavailable.
  std::string path; ///< Structural location of the recoverable condition.
  std::string detail; ///< Owned human-readable explanation.
  RecoveryAction action; ///< Recovery action applied to the affected content.
};

/**
 * @brief Fatal parsing failure classifications.
 * @ingroup model
 */
enum class ReadErrorKind {
  InvalidMagic, ///< Input does not begin with the class-file magic.
  TruncatedInput, ///< Input ends before a required field or record.
  InvalidVersion, ///< Declared class-file version is invalid for this operation.
  InvalidClass, ///< Class declaration or essential structure is invalid.
  InvalidConstantPool, ///< Constant-pool contents or framing are invalid.
  UnknownConstantTag, ///< Unknown constant encoding prevents safe framing.
  InvalidIndex, ///< Index is zero, reserved, or outside the constant pool.
  InvalidModifiedUtf8, ///< Bytes are not valid Java modified UTF-8.
  AttributeOverrun, ///< Declared attribute length exceeds its bounded parent.
  MalformedAttribute, ///< Attribute body violates its format.
  InvalidAttributeContext, ///< Attribute is not permitted at this structural location.
  VersionInapplicable, ///< Attribute or encoding is not applicable to the declared version.
  DuplicateAttribute, ///< A non-repeatable attribute occurs more than once.
  MalformedEssentialCode, ///< Required Code cannot be decoded or safely accepted.
  InvalidBranchTarget, ///< Branch displacement does not identify a valid instruction boundary.
  InvalidOpcode, ///< Opcode is reserved, unknown, or version-inapplicable.
  InvalidInstruction, ///< Instruction opcode or handle is invalid for the operation.
  ResourceLimitExceeded, ///< Configured work, memory, count, or depth budget was exhausted.
  UnsupportedDialect, ///< Input uses a class-file dialect not supported by the codec.
  TrailingBytes, ///< Bytes remain after the complete class-file envelope.
};

/**
 * @brief Fatal read failure with an absolute input byte offset and structural path.
 * @ingroup model
 */
struct ReadError {
  ReadErrorKind kind; ///< Discriminator selecting the interpretation of this record.
  std::size_t offset; ///< Absolute byte offset in the input class file.
  std::string path; ///< Structural location of the failure within the input class.
  std::string detail; ///< Owned human-readable explanation.
};

/**
 * @brief Fatal serialization and metadata-generation failure classifications.
 * @ingroup model
 */
enum class WriteErrorKind {
  InvalidClassModel, ///< Programmatic or edited class model violates emission requirements.
  InvalidConstantPoolReference, ///< Required constant reference or descriptor is invalid.
  ConstantPoolOverflow, ///< Emitted constant-pool count exceeds the u2 limit.
  AttributeTooLarge, ///< Attribute payload length cannot fit its wire field.
  CodeTooLarge, ///< Method code exceeds the permitted byte length.
  BranchOutOfRange, ///< Branch displacement cannot fit the selected encoding.
  InvalidSwitchTargets, ///< Switch keys or destinations cannot be encoded safely.
  InvalidLabel, ///< Label is foreign, stale, or structurally invalid.
  StaleCodeMetadata, ///< Metadata does not match the current code revision or layout.
  NeedsCodeMetadata, ///< Changed code requires complete caller-supplied or computed metadata.
  UnsafeOpaqueAttribute, ///< An opaque payload cannot safely survive the requested edit.
  UnsupportedOpaqueCode, ///< Opaque Code emission requires explicit opt-in or is incompatible with the operation.
  UnsupportedDialect, ///< Input uses a class-file dialect not supported by the codec.
  ResourceLimitExceeded, ///< Configured work, memory, count, or depth budget was exhausted.
  UnstableLayout, ///< Instruction layout did not converge within the permitted process.
  InvalidCodeLayout, ///< Code limits, instruction layout, or exception ranges are invalid.
  InvalidInstruction, ///< Instruction opcode or handle is invalid for the operation.
  NeedsFrameComputation, ///< The edit needs fresh verifier frames rather than maxima alone.
  MissingAnalysisContext, ///< Frame computation without an explicit context could not find a required class.
  StaleAnalysisResult, ///< Cached analysis does not match the current inputs.
  ClassResolutionFailure, ///< Required hierarchy information could not be resolved.
  InvalidStackShape, ///< Operand-stack categories or heights are incompatible.
  InvalidVerificationFrame, ///< Verification state violates the supported JVM rules.
  UnmappedCodeOffset, ///< An offset-bearing attribute cannot be mapped to the new code.
  StackMapOffsetOverflow, ///< An emitted stack-map offset cannot fit the wire format.
  UnrepresentableFrame, ///< Verification state cannot be represented as a JVM stack-map frame.
  UnsupportedSubroutineAnalysis, ///< Legacy jsr/ret analysis is unsupported; preservation may still succeed.
  Cancelled, ///< The supplied stop token requested cancellation.
};

/**
 * @brief Fatal serialization failure with a structural path and explanation.
 * @ingroup model
 */
struct WriteError {
  WriteErrorKind kind; ///< Discriminator selecting the interpretation of this record.
  std::string path; ///< Structural location of the failure, such as a method or attribute path.
  std::string detail; ///< Owned human-readable explanation.
};

} // namespace cafe
