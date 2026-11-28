/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

namespace cafe {

/**
 * @brief Requirements for emitting changed Code and its dependent metadata.
 * @ingroup model
 */
enum class CodeWritePolicy {
  RequireUnchangedOrSafe, ///< Preserve Code unless its existing metadata remains safe for the chosen output.
  CallerSuppliedMetadata, ///< Accept complete metadata explicitly supplied for the current revision and encoding.
};

/**
 * @brief Whether to preserve, compute maxima, or compute complete verifier metadata.
 * @ingroup model
 */
enum class CodeAnalysisMode {
  Preserve, ///< Preserve stored maxima and verifier metadata subject to safety checks.
  ComputeMaxs, ///< Compute stack/local limits without full reference assignability analysis.
  ComputeFrames, ///< Compute limits and verifier frames for emission. Supply an analysis::AnalysisContext for runtime and dependency resolution.
};

/**
 * @brief Policy for allocating constant-pool indices in the emitted class.
 * @ingroup model
 */
enum class ConstantPoolWritePolicy {
  Compact, ///< Emit reachable constants and deduplicate them where their JVM identity permits it.
  Preserve, ///< Retain existing slots and indices, including unused entries.
};

/**
 * @brief Class emission, metadata computation, and preservation policy. Defaults compact the
 * constant pool, honor bytecode encoding hints, and reject edits without safe metadata.
 * @ingroup model
 */
struct WriteOptions {
  CodeWritePolicy code_policy = CodeWritePolicy::RequireUnchangedOrSafe; ///< Policy governing metadata after instruction/handler changes.
  bool preserve_original_encodings = true; ///< Whether layout should honor retained wire-encoding hints.
  bool force_reencode = false; ///< Exercise instruction encoding even for otherwise unchanged Code.
  bool allow_opaque_code = false; ///< Permit verbatim emission of undecodable Code with constant-pool preservation.
  /**
   * @brief Metadata computation mode applied to every decoded Code attribute.
   * @see @ref cafe::write_class(const ClassFile&,const WriteOptions&,analysis::AnalysisContext&) "Writing with computed frames"
   */
  CodeAnalysisMode code_analysis = CodeAnalysisMode::Preserve;
  /**
   * @brief Allow analysis-mode emission to drop unrelocatable Code attributes with diagnostics.
   *
   * Permit dropping Code attributes whose offsets or dependencies cannot
   * be relocated safely. Applies only when code analysis is requested.
   */
  bool drop_unsafe_code_attributes = false;
  ConstantPoolWritePolicy constant_pool = ConstantPoolWritePolicy::Compact; ///< Selects how the output constant pool is built.
};

} // namespace cafe
