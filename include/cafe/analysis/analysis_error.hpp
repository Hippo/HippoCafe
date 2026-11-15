#pragma once

#include <optional>
#include <string>

#include "cafe/classpath/class_info.hpp"
#include "cafe/ids.hpp"

namespace cafe::analysis {

enum class AnalysisErrorKind {
  InvalidCode,
  MissingCode,
  InvalidDescriptor,
  InvalidConstantPoolReference,
  InvalidLabelOrRange,
  UnsupportedClassVersion,
  UnsupportedSubroutineAnalysis,
  StackUnderflow,
  InvalidCategoryOperation,
  IncompatibleJoin,
  TypeMismatch,
  UninitializedUse,
  InvalidConstructorState,
  TypeResolutionFailure,
  UnrepresentableFrame,
  AmbiguousTypeJoin,
  ResourceLimitExceeded,
  Cancelled,
  UnreachableCode,
  StackOverflow,
  LocalOverflow,
};

struct AnalysisError {
  AnalysisErrorKind kind = AnalysisErrorKind::InvalidCode;
  std::string class_name{};
  std::string method_name{};
  std::string descriptor{};
  std::optional<InstructionId> instruction{};
  std::optional<LabelId> label{};
  std::string detail{};
  std::optional<classpath::ResolutionError> resolution{};
};

} // namespace cafe::analysis
