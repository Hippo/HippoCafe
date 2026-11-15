#pragma once

namespace cafe {

enum class CodeWritePolicy { RequireUnchangedOrSafe,
                             CallerSuppliedMetadata };

enum class CodeAnalysisMode { Preserve,
                              ComputeMaxs,
                              ComputeFrames };

struct WriteOptions {
  CodeWritePolicy code_policy = CodeWritePolicy::RequireUnchangedOrSafe;
  bool preserve_original_encodings = true;
  bool force_reencode = false;
  bool allow_opaque_code = false;
  // Explicit computation visits every decoded Code attribute.
  CodeAnalysisMode code_analysis = CodeAnalysisMode::Preserve;
  // Permit dropping Code attributes whose offsets or dependencies cannot
  // be relocated safely. Applies only when code analysis is requested.
  bool drop_unsafe_code_attributes = false;
};

} // namespace cafe
