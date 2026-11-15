/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup io
 */

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include <cafe/class_file.hpp>
#include <cafe/error.hpp>
#include <cafe/read_options.hpp>

namespace cafe {

/**
 * @brief Owned parsed class and recoverable diagnostics. A successful read does not establish JVM
 * verifier validity.
 * @ingroup io
 */
struct ReadResult {
  ClassFile class_file; ///< Owned parsed class and its retained source storage.
  std::vector<Diagnostic> diagnostics; ///< Ordered recoverable conditions reported during the operation.
};

/**
 * @brief Parse a bounded class-file buffer by copying its input.
 *
 * The returned class owns its copy and does not borrow data. Configured budget failures become
 * ReadError; upstream std::bad_alloc may propagate. A compatible read may recover optional data
 * with diagnostics; success does not establish verifier validity or byte-for-byte fidelity.
 * @param data Complete class-file bytes borrowed only for this call.
 * @param options Parsing limits, allocation resource, and recovery policy.
 * @return An owning class and diagnostics, or a fatal ReadError.
 * @ingroup io
 */
[[nodiscard]] std::expected<ReadResult, ReadError> read_class(
    std::span<const std::byte> data, const ReadOptions& options = {});
/**
 * @brief Parse a bounded class-file buffer by taking ownership of its storage.
 *
 * The buffer may be consumed even if parsing fails. Configured budget failures become ReadError;
 * upstream std::bad_alloc may propagate. A compatible read may recover optional data with
 * diagnostics; success does not establish verifier validity or byte-for-byte fidelity.
 * @param data Complete class-file buffer moved into the reader; do not retain views into it.
 * @param options Parsing limits, allocation resource, and recovery policy.
 * @return An owning class and diagnostics, or a fatal ReadError.
 * @ingroup io
 */
[[nodiscard]] std::expected<ReadResult, ReadError> read_class(
    std::vector<std::byte>&& data, const ReadOptions& options = {});

} // namespace cafe
