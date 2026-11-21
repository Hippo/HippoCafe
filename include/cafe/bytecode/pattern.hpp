/**
 * @file
 * @brief Read-only pattern matching over instructions in method order.
 * @ingroup bytecode
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <cafe/bytecode/instruction_view.hpp>

namespace cafe {
class ClassFile;
}

namespace cafe::pattern {
namespace detail {
struct Program;
struct Source;
struct PatternAccess;
} // namespace detail

/// Exact optional filters for an ordinary method invocation. All text is copied by invoke().
struct InvokePattern {
  std::optional<Opcode> opcode{}; ///< Restrict the invocation opcode.
  std::optional<std::string_view> owner{}; ///< Restrict the JVM internal owner name.
  std::optional<std::string_view> name{}; ///< Restrict the target name.
  std::optional<std::string_view> descriptor{}; ///< Restrict the method descriptor.
};

/// Exact optional filters for a field access. All text is copied by field().
struct FieldPattern {
  std::optional<Opcode> opcode{}; ///< Restrict the access opcode.
  std::optional<std::string_view> owner{}; ///< Restrict the JVM internal owner name.
  std::optional<std::string_view> name{}; ///< Restrict the field name.
  std::optional<std::string_view> descriptor{}; ///< Restrict the field descriptor.
};

/// A copyable, immutable pattern that owns its operands and capture names.
class Pattern {
private:
  friend struct detail::PatternAccess;
  /// @param program Owned compiled pattern.
  explicit Pattern(std::shared_ptr<const detail::Program> program) : program(std::move(program)) {
  }
  std::shared_ptr<const detail::Program> program; ///< Immutable matching operations and capture names.
};

/// @param value Logical opcode to match.
/// @return A pattern matching the exact logical opcode.
[[nodiscard]] Pattern opcode(Opcode value);
/// Match exactly one instruction of any kind.
/// @return A pattern consuming any one instruction.
[[nodiscard]] Pattern any();
/// @param index Local slot to load, or nullopt for any integer load.
/// @return A pattern matching an integer local load.
[[nodiscard]] Pattern iload(std::optional<std::uint16_t> index = {});
/// @param index Local slot to store, or nullopt for any integer store.
/// @return A pattern matching an integer local store.
[[nodiscard]] Pattern istore(std::optional<std::uint16_t> index = {});
/// @param value Value pushed by iconst, bipush, sipush or an integer ldc, or nullopt for any such value.
/// @return A pattern matching a literal integer push or load.
[[nodiscard]] Pattern int_constant(std::optional<std::int32_t> value = {});
/// @param filter Optional opcode and symbolic member filters for an ordinary method invocation.
/// @return A pattern matching calls that satisfy the filters.
[[nodiscard]] Pattern invoke(const InvokePattern& filter);
/// @param filter Optional opcode and symbolic member filters for a field access.
/// @return A pattern matching accesses that satisfy the filters.
[[nodiscard]] Pattern field(const FieldPattern& filter);
/// @param patterns Patterns to match consecutively in the given order.
/// @return A pattern matching the concatenated parts.
[[nodiscard]] Pattern seq(std::initializer_list<Pattern> patterns);
/// @param patterns Alternatives in preference order. The first successful complete path wins.
/// @return A pattern matching one alternative.
[[nodiscard]] Pattern either(std::initializer_list<Pattern> patterns);
/// @param patterns Patterns to match consecutively.
/// @return A pattern matching the concatenated parts.
template<std::same_as<Pattern>... Parts>
[[nodiscard]] Pattern seq(Parts... patterns) {
  return seq({std::move(patterns)...});
}
/// @param patterns Alternatives in preference order.
/// @return A pattern matching one alternative.
template<std::same_as<Pattern>... Parts>
[[nodiscard]] Pattern either(Parts... patterns) {
  return either({std::move(patterns)...});
}
/// @param pattern Pattern to match greedily zero or one times.
/// @return A greedy optional pattern.
[[nodiscard]] Pattern optional(const Pattern& pattern);
/// @param pattern Repeated pattern.
/// @param min Inclusive minimum repetition count.
/// @param max Inclusive maximum repetition count. A smaller maximum makes the pattern invalid.
/// @return A bounded greedy repetition.
[[nodiscard]] Pattern repeat(const Pattern& pattern, std::size_t min, std::size_t max);
/// @param name Unique, nonempty name of the capture. Repetition records its last participation.
/// @param pattern Pattern whose matched instructions to capture.
/// @return A pattern recording the instructions consumed by its argument.
[[nodiscard]] Pattern capture(std::string_view name, const Pattern& pattern);

/// Options for leftmost-first searches in linear instruction order.
struct MatchOptions {
  bool overlap = false; ///< Resume one instruction after the previous start when true.
  std::size_t max_work = std::size_t{32} * 1024 * 1024; ///< Maximum execution steps across all candidate starts.
};

/// Reason a search could not return a complete answer.
enum class MatchErrorKind {
  InvalidMethod, ///< Method index is out of range.
  NoCode, ///< Method lacks decoded Code.
  InvalidPattern, ///< Malformed composition, capture declaration or member opcode restriction.
  InvalidOperand, ///< A candidate's symbolic operand could not be resolved.
  WorkLimit, ///< Search exhausted its configured work budget.
};

/// Search failure with static explanatory text and an optional offending instruction.
struct MatchError {
  MatchErrorKind kind; ///< Machine-readable failure.
  std::string_view detail; ///< Process-lifetime diagnostic text.
  std::optional<InstructionId> instruction{}; ///< Candidate instruction when a symbolic operand was invalid.
};

/// A participating capture. An empty participating capture has an empty instruction span.
/// Moving a capture leaves its source with an empty instruction span.
class Capture {
public:
  /// @return Original instruction views. Keep the class and this capture or its match alive.
  [[nodiscard]] std::span<const InstructionView> instructions() const noexcept;

private:
  friend class Match;
  /// @param source Captured instruction storage.
  /// @param first First index in source.
  /// @param length Number of captured instructions.
  Capture(std::shared_ptr<const detail::Source> source, std::size_t first, std::size_t length);
  std::shared_ptr<const detail::Source> source; ///< Shared snapshot retaining the capture.
  std::size_t first; ///< First index in the snapshot.
  std::size_t length; ///< Number of participating instructions.
};

/// A consuming contiguous match. Its original IDs survive unrelated edits. Keep the source class alive.
/// A moved-from match is empty, has an invalid start ID, and is rejected by the editor as stale.
class Match {
public:
  /// @return Stable ID for the first matched instruction.
  [[nodiscard]] InstructionId start() const noexcept;
  /// @return Zero-based instruction ordinal at search time.
  [[nodiscard]] std::size_t start_index() const noexcept;
  /// @return Number of instructions in the matched range.
  [[nodiscard]] std::size_t size() const noexcept;
  /// @return Ordered stable IDs. The returned span borrows the match's snapshot.
  [[nodiscard]] std::span<const InstructionId> instruction_ids() const noexcept;
  /// @return Method scope at search time.
  [[nodiscard]] std::uint32_t source_scope() const noexcept;
  /// @return Code revision at search time.
  [[nodiscard]] std::uint64_t source_revision() const noexcept;
  /// @param name Capture declaration to look up.
  /// @return Engaged capture if its declaration participated, or nullopt for an absent/unknown name.
  [[nodiscard]] std::optional<Capture> capture(std::string_view name) const noexcept;

private:
  friend struct detail::PatternAccess;
  /// Original half-open instruction interval for a participating capture.
  using Bounds = std::pair<std::size_t, std::size_t>;
  /// @param source Retained snapshot of the matched method.
  /// @param first First index in source.
  /// @param length Number of matched instructions.
  /// @param groups Bounds of each capture in declaration order.
  Match(std::shared_ptr<const detail::Source> source, std::size_t first, std::size_t length,
        std::vector<std::optional<Bounds>> groups);
  std::shared_ptr<const detail::Source> source; ///< Shared source method and capture names.
  std::size_t first; ///< First index in source.
  std::size_t length; ///< Number of matched instructions.
  std::vector<std::optional<Bounds>> groups; ///< Half-open bounds for participating captures.
};

/// @param file Source class, which must outlive use of returned views.
/// @param method_index Zero-based method index.
/// @param pattern Pattern to search for. Whole matches consume at least one instruction.
/// @param options Overlap and work settings.
/// @return Matches in ascending start order, or a search error.
[[nodiscard]] std::expected<std::vector<Match>, MatchError> find_all(
    const ClassFile& file, std::size_t method_index, const Pattern& pattern, MatchOptions options = {});
/// @param file Source class, which must outlive use of returned views.
/// @param method_index Zero-based method index.
/// @param pattern Pattern to search for. Whole matches consume at least one instruction.
/// @param options Work settings.
/// @return First consuming match, nullopt when absent, or a search error.
[[nodiscard]] std::expected<std::optional<Match>, MatchError> find_first(
    const ClassFile& file, std::size_t method_index, const Pattern& pattern, MatchOptions options = {});

} // namespace cafe::pattern
