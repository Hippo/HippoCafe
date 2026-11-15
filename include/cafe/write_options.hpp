#pragma once

namespace cafe {

enum class CodeWritePolicy { RequireUnchangedOrSafe,
                             CallerSuppliedMetadata };

struct WriteOptions {
  CodeWritePolicy code_policy = CodeWritePolicy::RequireUnchangedOrSafe;
  bool preserve_original_encodings = true;
  bool force_reencode = false;
  bool allow_opaque_code = false;
};

} // namespace cafe
