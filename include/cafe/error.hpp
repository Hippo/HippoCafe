#pragma once

#include <cstddef>
#include <string>

namespace cafe {

enum class Severity { Info,
                      Warning,
                      Error };
enum class RecoveryAction { None,
                            Dropped,
                            PreservedOpaque,
                            RetainedRaw };
enum class DiagnosticCode {
  DroppedMalformedAttribute,
  VersionInapplicable,
  InvalidAttributeContext,
  InvalidAttributeName,
  DuplicateAttribute,
  InvalidModifiedUtf8,
  InvalidVersion,
  TrailingBytes,
  OpaqueEssentialCode,
  DiagnosticsSuppressed,
  DroppedUnsafeCodeAttribute,
};

struct Diagnostic {
  Severity severity;
  DiagnosticCode code;
  std::size_t offset;
  std::string path;
  std::string detail;
  RecoveryAction action;
};

enum class ReadErrorKind {
  InvalidMagic,
  TruncatedInput,
  InvalidVersion,
  InvalidClass,
  InvalidConstantPool,
  UnknownConstantTag,
  InvalidIndex,
  InvalidModifiedUtf8,
  AttributeOverrun,
  MalformedAttribute,
  InvalidAttributeContext,
  VersionInapplicable,
  DuplicateAttribute,
  MalformedEssentialCode,
  InvalidBranchTarget,
  InvalidOpcode,
  InvalidInstruction,
  ResourceLimitExceeded,
  UnsupportedDialect,
  TrailingBytes,
};

struct ReadError {
  ReadErrorKind kind;
  std::size_t offset;
  std::string path;
  std::string detail;
};

enum class WriteErrorKind {
  InvalidClassModel,
  InvalidConstantPoolReference,
  ConstantPoolOverflow,
  AttributeTooLarge,
  CodeTooLarge,
  BranchOutOfRange,
  InvalidSwitchTargets,
  InvalidLabel,
  StaleCodeMetadata,
  NeedsCodeMetadata,
  UnsafeOpaqueAttribute,
  UnsupportedOpaqueCode,
  UnsupportedDialect,
  ResourceLimitExceeded,
  UnstableLayout,
  InvalidCodeLayout,
  InvalidInstruction,
  NeedsFrameComputation,
  MissingAnalysisContext,
  StaleAnalysisResult,
  ClassResolutionFailure,
  InvalidStackShape,
  InvalidVerificationFrame,
  UnmappedCodeOffset,
  StackMapOffsetOverflow,
  UnrepresentableFrame,
  UnsupportedSubroutineAnalysis,
  Cancelled,
};

struct WriteError {
  WriteErrorKind kind;
  std::string path;
  std::string detail;
};

} // namespace cafe
