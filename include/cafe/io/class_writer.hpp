/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup io
 */

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include <cafe/class_file.hpp>
#include <cafe/error.hpp>
#include <cafe/write_options.hpp>

namespace cafe {

namespace analysis {
class AnalysisContext;
}

/**
 * @brief Owned serialized class bytes and emission diagnostics. Writing leaves the input class
 * unchanged.
 * @ingroup io
 */
struct WriteResult {
  std::vector<std::byte> bytes; ///< Owned complete serialized class-file bytes.
  std::vector<Diagnostic> diagnostics; ///< Ordered recoverable conditions reported during the operation.
};

/**
 * @brief Serialize a class using the default analysis environment.
 *
 * By default, the writer rebuilds the output constant pool from reachable references.
 * Set WriteOptions::constant_pool to ConstantPoolWritePolicy::Preserve when an
 * opaque attribute depends on its original indices or a byte-preserving round trip is needed.
 *
 * CodeAnalysisMode::ComputeMaxs can compute stack and local limits without a hierarchy.
 * CodeAnalysisMode::ComputeFrames starts with an empty classpath. If a required class
 * cannot be found, the writer returns WriteErrorKind::MissingAnalysisContext. Use
 * the overload that takes an analysis::AnalysisContext to supply runtime and application classes.
 *
 * Allocation failures may propagate. Configured analysis budget failures return
 * WriteErrorKind::ResourceLimitExceeded.
 * @param class_file Class model to serialize without mutation.
 * @param options Encoding, preservation, and verification-metadata policy.
 * @return Owned output bytes and diagnostics, or a WriteError; no partial output is returned.
 * @ingroup io
 */
[[nodiscard]] std::expected<WriteResult, WriteError> write_class(
    const ClassFile& class_file, const WriteOptions& options = {});

/**
 * @brief Serialize a class using an explicit hierarchy and analysis limits.
 *
 * Select CodeAnalysisMode::ComputeFrames in WriteOptions::code_analysis to recompute
 * max stack, max locals, and verifier frames. The writer emits StackMapTable where needed
 * and leaves the input class unchanged.
 *
 * Build the context from the target runtime and any application dependencies. Frame analysis
 * automatically includes the class being written. Missing definitions in the supplied classpath
 * can produce WriteErrorKind::ClassResolutionFailure.
 *
 * Allocation failures may propagate. Configured analysis budget failures return
 * WriteErrorKind::ResourceLimitExceeded.
 *
 * @param class_file Class model to serialize without mutation.
 * @param options Encoding, preservation, and verification-metadata policy.
 * @param context Hierarchy and resource policy for verification computation.
 * @return Owned output bytes and diagnostics, or a WriteError; no partial output is returned.
 * @ingroup io
 */
[[nodiscard]] std::expected<WriteResult, WriteError> write_class(
    const ClassFile& class_file, const WriteOptions& options, analysis::AnalysisContext& context);

/**
 * @brief Write a class with recomputed frames, max stack, and max locals using a target JDK.
 * @snippet edit_and_compute.cpp compute-frames
 * @param class_file Class to serialize without mutation.
 * @param java_home Target Java installation containing jmods or rt.jar.
 * @param target_release Java release used to select multi-release JAR entries and bound resolved versions.
 * @param dependencies Ordered application class directories, JARs, and JMODs.
 * @param options Other emission options. The analysis mode is set to ComputeFrames.
 * @return Owned output bytes and diagnostics, or a classpath or emission error.
 * @ingroup io
 */
[[nodiscard]] std::expected<WriteResult, WriteError> write_class_with_frames(
    const ClassFile& class_file, const std::filesystem::path& java_home, std::uint16_t target_release,
    std::span<const std::filesystem::path> dependencies = {}, WriteOptions options = {});

} // namespace cafe
