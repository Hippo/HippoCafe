/**
 * @file
 * @brief Construction and bounded evaluation of instruction patterns.
 * @ingroup implementation
 */

#include <cafe/bytecode/pattern.hpp>

#include <algorithm>
#include <bit>
#include <string>

#include <cafe/class_file.hpp>

#include "bytecode/code_access.hpp"
#include "io/code_operands.hpp"
#include "model/class_storage.hpp"
#include "model/constant_validation.hpp"

namespace cafe::pattern {
namespace detail {

/// Instructions for an ordered matching program. Jump offsets are relative to the operation itself.
enum class Action {
  Any, ///< Consume one instruction.
  Opcode, ///< Compare the logical opcode.
  Local, ///< Compare a local opcode and optional slot.
  Integer, ///< Compare a literal integer load.
  Invoke, ///< Match a symbolic method invocation.
  Field, ///< Match a symbolic field access.
  Split, ///< Save a lower-priority alternative.
  Jump, ///< Jump forward over alternatives.
  Fail, ///< Backtrack to the last alternative.
  Open, ///< Record the beginning of a capture.
  Close, ///< Record the end of a capture.
  Reset, ///< Initialize a repetition counter.
  Repeat, ///< Check repetition bounds and offer the remaining suffix.
  Back, ///< Jump back to a repetition check.
};

/// Owned symbolic filters shared when programs are composed.
struct MemberFilter {
  std::optional<Opcode> opcode; ///< Optional logical opcode restriction.
  std::optional<std::string> owner; ///< Optional exact owner name.
  std::optional<std::string> name; ///< Optional exact member name.
  std::optional<std::string> descriptor; ///< Optional exact descriptor.

  /// @param filter Invocation or field filters to own.
  template<typename T>
  explicit MemberFilter(const T& filter) :
      opcode(filter.opcode), owner(filter.owner), name(filter.name), descriptor(filter.descriptor) {
  }

  /// @param member Resolved invocation or field access.
  /// @return True when all supplied text predicates match.
  template<typename T>
  bool accepts(const T& member) const {
    return (!owner || *owner == member.owner) && (!name || *name == member.name) &&
           (!descriptor || *descriptor == member.descriptor);
  }
};

/// A predicate or control operation. Repetition bounds stay compact even for very large maxima.
struct Step {
  Action action; ///< Operation to execute.
  std::size_t argument = 0; ///< Jump displacement, capture slot, or logical opcode.
  std::size_t min = 0; ///< Inclusive minimum repetitions.
  std::size_t max = 0; ///< Inclusive maximum repetitions.
  std::optional<std::int32_t> operand{}; ///< Literal or local index restriction.
  std::shared_ptr<const MemberFilter> member{}; ///< Owned filters for a symbolic predicate.
};

/// A flat immutable program when published as a Pattern.
struct Program {
  std::vector<Step> steps; ///< Steps in execution order.
  std::vector<std::string> names; ///< Capture names indexed by slot.
  std::string_view error{}; ///< Static explanation of an invalid composition.
};

/// Match and capture snapshots share instruction storage and owned capture names.
struct Source {
  std::shared_ptr<const Program> program; ///< Program retaining capture declarations.
  std::uint32_t scope; ///< Code owner identity at match time.
  std::uint64_t revision; ///< Code revision at match time.
  std::vector<InstructionView> views; ///< Cached prefix of inspected instructions.
  std::vector<InstructionId> ids; ///< Stable IDs in the same order as views.
};

/// Internal construction access for immutable public values.
struct PatternAccess {
  /// @param program Validated or failed composition.
  /// @return An immutable pattern owning the program.
  static Pattern finish(Program program) {
    return Pattern(std::make_shared<const Program>(std::move(program)));
  }

  /// @param value Compiled pattern.
  /// @return Its program, or an empty pointer after a move.
  static const std::shared_ptr<const Program>& get(const Pattern& value) {
    return value.program;
  }

  /// @param source Observed method and instruction views.
  /// @param first First participating ordinal.
  /// @param end First ordinal after the match.
  /// @param groups Optional half-open bounds indexed by capture slot.
  /// @return An owning result sharing the source snapshot.
  static Match result(const std::shared_ptr<const Source>& source, std::size_t first, std::size_t end,
                      std::vector<std::optional<Match::Bounds>> groups) {
    return Match(source, first, end - first, std::move(groups));
  }
};
} // namespace detail

namespace {
using detail::Action;
using detail::PatternAccess;
using detail::Program;
using detail::Step;

/// Append a whole program, remapping capture slots but preserving relative jump displacements.
/// @param program Destination program under construction.
/// @param part Source pattern to append.
void append(Program& program, const Pattern& part) {
  const auto& fragment = PatternAccess::get(part);
  if (!fragment || !fragment->error.empty()) {
    program.error = fragment ? fragment->error : "moved-from pattern";
    return;
  }
  const auto offset = program.names.size();
  for (const auto& name : fragment->names) {
    if (std::ranges::find(program.names, name) != program.names.end()) {
      program.error = "duplicate capture declaration";
      return;
    }
    program.names.push_back(name);
  }
  for (auto step : fragment->steps) {
    if (step.action == Action::Open || step.action == Action::Close)
      step.argument += offset;
    program.steps.push_back(std::move(step));
  }
}

/// @param step Atomic predicate to test.
/// @return A single-predicate pattern.
Pattern predicate(Step step) {
  Program program;
  program.steps.push_back(std::move(step));
  return PatternAccess::finish(std::move(program));
}

/// @param action Invocation or field predicate.
/// @param filter Optional opcode and symbolic restrictions.
/// @param first First supported opcode.
/// @param last Final supported opcode.
/// @return A member pattern, possibly carrying an invalid opcode diagnostic.
template<typename T>
Pattern member_predicate(Action action, const T& filter, Opcode first, Opcode last) {
  if (filter.opcode && (*filter.opcode < first || *filter.opcode > last)) {
    Program invalid;
    invalid.error = "opcode outside requested member family";
    return PatternAccess::finish(std::move(invalid));
  }
  Step step{action};
  step.member = std::make_shared<const detail::MemberFilter>(filter);
  return predicate(std::move(step));
}
} // namespace

Pattern opcode(Opcode value) {
  return predicate({Action::Opcode, static_cast<std::size_t>(value)});
}

Pattern any() {
  return predicate({Action::Any});
}

Pattern iload(std::optional<std::uint16_t> index) {
  return predicate({.action = Action::Local, .argument = static_cast<std::size_t>(Opcode::ILoad), .operand = index});
}

Pattern istore(std::optional<std::uint16_t> index) {
  return predicate({.action = Action::Local, .argument = static_cast<std::size_t>(Opcode::IStore), .operand = index});
}

Pattern int_constant(std::optional<std::int32_t> value) {
  return predicate({.action = Action::Integer, .operand = value});
}

Pattern invoke(const InvokePattern& filter) {
  return member_predicate(Action::Invoke, filter, Opcode::InvokeVirtual, Opcode::InvokeInterface);
}

Pattern field(const FieldPattern& filter) {
  return member_predicate(Action::Field, filter, Opcode::GetStatic, Opcode::PutField);
}

Pattern seq(std::initializer_list<Pattern> patterns) {
  Program program;
  for (const auto& part : patterns)
    append(program, part);
  return PatternAccess::finish(std::move(program));
}

Pattern either(std::initializer_list<Pattern> patterns) {
  Program program;
  if (patterns.size() == 0)
    program.steps.push_back({Action::Fail});
  std::vector<std::size_t> exits;
  for (auto it = patterns.begin(); it != patterns.end(); ++it) {
    const auto split = program.steps.size();
    const bool last = it + 1 == patterns.end();
    if (!last)
      program.steps.push_back({Action::Split});
    append(program, *it);
    if (!last) {
      exits.push_back(program.steps.size());
      program.steps.push_back({Action::Jump});
      program.steps[split].argument = program.steps.size() - split;
    }
  }
  for (auto exit : exits)
    program.steps[exit].argument = program.steps.size() - exit;
  return PatternAccess::finish(std::move(program));
}

Pattern optional(const Pattern& pattern) {
  return repeat(pattern, 0, 1);
}

Pattern repeat(const Pattern& pattern, std::size_t min, std::size_t max) {
  Program program;
  if (min > max)
    program.error = "minimum repetition count exceeds maximum";
  program.steps.push_back({Action::Reset});
  program.steps.push_back({Action::Repeat, 0, min, max});
  append(program, pattern);
  program.steps.push_back({Action::Back, program.steps.size() - 1});
  program.steps[1].argument = program.steps.size() - 1;
  return PatternAccess::finish(std::move(program));
}

Pattern capture(std::string_view name, const Pattern& pattern) {
  Program program;
  if (name.empty())
    program.error = "capture name is empty";
  program.names.emplace_back(name);
  program.steps.push_back({Action::Open});
  append(program, pattern);
  program.steps.push_back({Action::Close});
  return PatternAccess::finish(std::move(program));
}

Capture::Capture(std::shared_ptr<const detail::Source> source, std::size_t first, std::size_t length) :
    source(std::move(source)), first(first), length(length) {
}

std::span<const InstructionView> Capture::instructions() const noexcept {
  if (!source)
    return {};
  return std::span(source->views).subspan(first, length);
}

Match::Match(std::shared_ptr<const detail::Source> source, std::size_t first, std::size_t length,
             std::vector<std::optional<Bounds>> groups) :
    source(std::move(source)), first(first), length(length), groups(std::move(groups)) {
}

InstructionId Match::start() const noexcept {
  return source ? source->ids[first] : InstructionId{};
}

std::size_t Match::start_index() const noexcept {
  return source ? first : 0;
}

std::size_t Match::size() const noexcept {
  return source ? length : 0;
}

std::span<const InstructionId> Match::instruction_ids() const noexcept {
  if (!source)
    return {};
  return std::span(source->ids).subspan(first, length);
}

std::uint32_t Match::source_scope() const noexcept {
  return source ? source->scope : 0;
}

std::uint64_t Match::source_revision() const noexcept {
  return source ? source->revision : 0;
}

std::optional<Capture> Match::capture(std::string_view name) const noexcept {
  if (!source)
    return std::nullopt;
  const auto& names = source->program->names;
  const auto at = std::ranges::find(names, name);
  if (at == names.end())
    return std::nullopt;
  const auto& bounds = groups[static_cast<std::size_t>(at - names.begin())];
  if (!bounds)
    return std::nullopt;
  return Capture(source, bounds->first, bounds->second - bounds->first);
}

namespace {
/// Class metadata shared by the class-scoped and method-scoped entry points.
struct MatchInput {
  const ConstantPool& pool; ///< Pool supplying loadable constants.
  ClassVersion version; ///< Instruction validation version.
  std::span<const Attribute> attributes; ///< Class attributes supplying bootstrap methods.
};

/// @param instruction Instruction whose operand could not be resolved.
/// @return An invalid-operand error carrying the stable instruction ID.
std::unexpected<MatchError> bad_operand(const InstructionView& instruction) {
  return std::unexpected(MatchError{MatchErrorKind::InvalidOperand, "invalid constant-pool operand", instruction.id()});
}

/// @return Search work-limit error.
std::unexpected<MatchError> work_limit() {
  return std::unexpected(MatchError{MatchErrorKind::WorkLimit, "pattern search work limit exceeded"});
}

/// Resolve integer literal loads, distinguishing non-integer literals from malformed references.
/// @param input Class supplying loadable constants.
/// @param instruction Candidate instruction.
/// @param checked Shared validation cache for pool operands reached in this search.
/// @param budget Remaining search work.
/// @return An integer literal, nullopt for other operands, or a validation/work error.
std::expected<std::optional<std::int32_t>, MatchError> integer(MatchInput input, const InstructionView& instruction,
                                                               cafe::detail::ConstantOperandValidator& checked, std::size_t& budget) {
  const auto opcode = instruction.opcode();
  if (opcode >= Opcode::IConstM1 && opcode <= Opcode::IConst5)
    return static_cast<std::int32_t>(opcode) - static_cast<std::int32_t>(Opcode::IConst0);
  if (opcode != Opcode::Ldc)
    return instruction.immediate();
  const auto raw = instruction.raw();
  const auto index = raw.constant_index();
  if (!index)
    return bad_operand(instruction);
  const auto& pool = input.pool;
  const auto* entry = pool.get(*index);
  if (!entry || cafe::detail::validate_instruction(raw, input.version, pool, 0))
    return bad_operand(instruction);
  if (entry->tag == CpTag::Integer)
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(entry->bits));
  const auto validity = checked.validate(*index, pool, input.version, input.attributes, &budget);
  if (validity == cafe::detail::OperandValidity::WorkLimit)
    return work_limit();
  if (validity != cafe::detail::OperandValidity::Valid)
    return bad_operand(instruction);
  return std::nullopt;
}

/// @param filter Symbolic restrictions to check.
/// @param insn Candidate instruction.
/// @param member Typed operand resolution result.
/// @return Whether the target matches, or a resolution failure.
template<typename T>
std::expected<bool, MatchError> check_member(const detail::MemberFilter& filter, const InstructionView& insn,
                                             const std::expected<T, CodeEditError>& member) {
  if (!member)
    return bad_operand(insn);
  return filter.accepts(*member);
}

/// @param input Class supplying the constant pool.
/// @param step Single consuming predicate.
/// @param insn Candidate instruction.
/// @param checked Shared validation cache for constants.
/// @param budget Remaining search work.
/// @return Whether the candidate satisfies the predicate, or an invalid operand.
std::expected<bool, MatchError> accepts(MatchInput input, const Step& step, const InstructionView& insn,
                                        cafe::detail::ConstantOperandValidator& checked, std::size_t& budget) {
  switch (step.action) {
    case Action::Any:
      return true;
    case Action::Opcode:
      return insn.is(static_cast<Opcode>(step.argument));
    case Action::Local:
      return insn.is(static_cast<Opcode>(step.argument)) && (!step.operand || insn.local_index() == step.operand);
    case Action::Integer: {
      auto value = integer(input, insn, checked, budget);
      if (!value)
        return std::unexpected(value.error());
      return value->has_value() && (!step.operand || *value == step.operand);
    }
    case Action::Invoke:
      if (!insn.is_invoke() || (step.member->opcode && !insn.is(*step.member->opcode)))
        return false;
      return check_member(*step.member, insn, insn.method_invocation());
    case Action::Field:
      if (!insn.is_field_access() || (step.member->opcode && !insn.is(*step.member->opcode)))
        return false;
      return check_member(*step.member, insn, insn.field_access());
    default:
      return false;
  }
}

/// Scratch slots hold half-open capture bounds, or a repetition count and its previous cursor.
using Slot = std::optional<std::pair<std::size_t, std::size_t>>;

/// Charge before allocating, clearing, or copying a range of scratch or result slots.
/// @param budget Remaining work, decremented on success.
/// @param work Amount to charge.
/// @return Whether the budget covered the requested work.
bool spend(std::size_t& budget, std::size_t work) noexcept {
  if (work > budget)
    return false;
  budget -= work;
  return true;
}

/// Saved location of a lower-priority continuation.
struct Choice {
  std::size_t pc; ///< Program counter to resume.
  std::size_t cursor; ///< Input position to resume.
  std::size_t checkpoint; ///< Undo-log length to restore.
};

/// Undo record for a changed capture or repetition slot.
struct Change {
  std::size_t slot; ///< Index of the affected scratch slot.
  Slot previous; ///< Slot contents before the recorded change.
};

/// A prioritized, bounded iterative matcher. Alternatives restore the undo log before resuming.
/// The input cache is filled on demand, so find_first can finish before reading later candidates.
class Matcher {
public:
  /// @param input Class supplying symbolic operands.
  /// @param range Input views in method order.
  /// @param code Owner supplying instruction identity and revision.
  /// @param program Immutable compiled pattern.
  /// @param budget Work remaining after preparation.
  Matcher(MatchInput input, InstructionViewRange range, const MethodCode& code,
          std::shared_ptr<const Program> program, std::size_t budget) :
      input(input), next_view(range.begin()), count(range.size()), budget(budget),
      source(std::make_shared<detail::Source>(detail::Source{
          std::move(program), cafe::detail::BytecodeAccess::scope(code), code.revision(), {}, {}})),
      slots(source->program->names.size() + source->program->steps.size()) {
  }

  /// @param first_only Stop after the first consuming match.
  /// @param overlap Try the next input position after each match start.
  /// @return Ordered consuming matches or a search failure.
  std::expected<std::vector<Match>, MatchError> run(bool first_only, bool overlap) {
    std::vector<Match> matches;
    for (std::size_t start = 0; start < count;) {
      auto match = attempt(start);
      if (!match)
        return std::unexpected(match.error());
      if (!*match) {
        ++start;
        continue;
      }
      const auto length = (*match)->size();
      matches.push_back(std::move(**match));
      if (first_only)
        break;
      start += overlap ? 1 : length;
    }
    return matches;
  }

private:
  /// @param index Zero-based instruction ordinal in the method.
  /// @return Cached instruction view at the requested position.
  const InstructionView& view(std::size_t index) {
    while (index >= source->views.size()) {
      const auto insn = *next_view++;
      source->views.push_back(insn);
      source->ids.push_back(insn.id());
    }
    return source->views[index];
  }

  /// @param slot Capture or repetition slot to update.
  /// @param value New scratch contents.
  void write(std::size_t slot, Slot value) {
    if (!choices.empty())
      changes.push_back({slot, slots[slot]});
    slots[slot] = value;
  }

  /// @param pc Program counter to restore.
  /// @param cursor Input cursor to restore.
  /// @return True when an alternative was restored, or a work-limit failure.
  std::expected<bool, MatchError> backtrack(std::size_t& pc, std::size_t& cursor) {
    if (choices.empty())
      return false;
    const auto choice = choices.back();
    choices.pop_back();
    if (!spend(budget, changes.size() - choice.checkpoint))
      return work_limit();
    while (changes.size() > choice.checkpoint) {
      const auto& change = changes.back();
      slots[change.slot] = change.previous;
      changes.pop_back();
    }
    pc = choice.pc;
    cursor = choice.cursor;
    return true;
  }

  /// @param start Candidate start ordinal.
  /// @return The first consuming result at this start, an empty optional, or a search failure.
  std::expected<std::optional<Match>, MatchError> attempt(std::size_t start) {
    const auto& program = *source->program;
    const auto captures = program.names.size();
    // Each active repeat is initialized by Reset, so only capture slots require clearing.
    if (!spend(budget, captures))
      return work_limit();
    std::fill_n(slots.begin(), captures, std::nullopt);
    choices.clear();
    changes.clear();
    std::size_t pc = 0;
    auto cursor = start;
    for (;;) {
      if (!spend(budget, 1))
        return work_limit();
      if (pc == program.steps.size()) {
        if (cursor > start) {
          if (!spend(budget, captures))
            return work_limit();
          std::vector<Slot> groups(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(captures));
          return PatternAccess::result(source, start, cursor, std::move(groups));
        }
      } else {
        const auto& step = program.steps[pc];
        switch (step.action) {
          case Action::Split:
            choices.push_back({pc + step.argument, cursor, changes.size()});
            ++pc;
            continue;
          case Action::Jump:
            pc += step.argument;
            continue;
          case Action::Back:
            pc -= step.argument;
            continue;
          case Action::Open:
            write(step.argument, std::pair{cursor, cursor});
            ++pc;
            continue;
          case Action::Close:
            // Each Close is compiled after its Open, including when a choice restores the slot.
            write(step.argument, std::pair{slots[step.argument].value().first, cursor}); // NOLINT(bugprone-unchecked-optional-access)
            ++pc;
            continue;
          case Action::Reset:
            write(captures + pc + 1, std::pair{std::size_t{0}, cursor});
            ++pc;
            continue;
          case Action::Repeat: {
            const auto slot = captures + pc;
            // Every path to this repeat started with Reset, and backtracking restores its state.
            const auto [completed, previous] = slots[slot].value(); // NOLINT(bugprone-unchecked-optional-access)
            if (completed >= step.min) {
              // Mandatory empty iterations satisfy the minimum. The first optional iteration
              // still has priority over exiting and may consume through a lower alternative.
              if (completed == step.max || (completed > step.min && previous == cursor)) {
                pc += step.argument;
                continue;
              }
              choices.push_back({pc + step.argument, cursor, changes.size()});
            }
            write(slot, std::pair{completed + 1, cursor});
            ++pc;
            continue;
          }
          case Action::Fail:
            break;
          default:
            if (cursor < count) {
              auto accepted = accepts(input, step, view(cursor), checked, budget);
              if (!accepted)
                return std::unexpected(accepted.error());
              if (*accepted) {
                ++cursor;
                ++pc;
                continue;
              }
            }
            break;
        }
      }
      auto retry = backtrack(pc, cursor);
      if (!retry)
        return std::unexpected(retry.error());
      if (!*retry)
        return std::nullopt;
    }
  }

  MatchInput input; ///< Class supplying symbolic operands.
  InstructionViewIterator next_view; ///< Next input instruction to cache.
  std::size_t count; ///< Number of instructions available for search.
  std::size_t budget; ///< Remaining allowed work for the search.
  std::shared_ptr<detail::Source> source; ///< Observation data shared with matches and captures.
  std::vector<Slot> slots; ///< Capture bounds and counted-repeat state.
  std::vector<Choice> choices; ///< Pending alternatives in last-in, first-out order.
  std::vector<Change> changes; ///< Updates to restore while backtracking.
  cafe::detail::ConstantOperandValidator checked; ///< Reachable pool operands already checked in this search.
};

/// @param input Class supplying loadable constants.
/// @param range Decoded instruction views in method order.
/// @param body Decoded body, or null if unavailable.
/// @param pattern Pattern to execute.
/// @param options Overlap and work limits.
/// @param first_only Stop after the earliest match.
/// @return Matching ranges or a validation/execution error.
std::expected<std::vector<Match>, MatchError> search(MatchInput input, InstructionViewRange range, const CodeAttribute* body,
                                                     const Pattern& pattern, MatchOptions options, bool first_only) {
  if (!body)
    return std::unexpected(MatchError{MatchErrorKind::NoCode, "method has no decoded Code"});
  const auto& program = PatternAccess::get(pattern);
  if (!program || !program->error.empty())
    return std::unexpected(MatchError{MatchErrorKind::InvalidPattern, program ? program->error : "moved-from pattern"});
  if (range.empty())
    return std::vector<Match>{};
  if (!spend(options.max_work, program->steps.size()) || !spend(options.max_work, program->names.size()))
    return work_limit();
  Matcher matcher(input, range, body->code(), program, options.max_work);
  return matcher.run(first_only, options.overlap);
}

/// @param file Source class.
/// @param method_index Method-table index.
/// @param pattern Pattern to execute.
/// @param options Overlap and work limits.
/// @param first_only Stop after the earliest match.
/// @return Matching ranges or a validation/execution error.
std::expected<std::vector<Match>, MatchError> search(const ClassFile& file, std::size_t method_index,
                                                     const Pattern& pattern, MatchOptions options, bool first_only) {
  if (method_index >= file.methods().size())
    return std::unexpected(MatchError{MatchErrorKind::InvalidMethod, "invalid method index"});
  return search({file.constant_pool(), file.version(), file.attributes()}, file.instructions(method_index),
                file.methods()[method_index].code(), pattern, options, first_only);
}

/// @param results Search result containing at most one match.
/// @return The first match, an empty optional, or a search error.
std::expected<std::optional<Match>, MatchError> first_match(std::expected<std::vector<Match>, MatchError> results) {
  return std::move(results).transform([](auto matches) -> std::optional<Match> {
    if (matches.empty())
      return std::nullopt;
    return std::move(matches.front());
  });
}
} // namespace

std::expected<std::vector<Match>, MatchError> find_all(const ClassFile& file, std::size_t method_index,
                                                       const Pattern& pattern, MatchOptions options) {
  return search(file, method_index, pattern, options, false);
}

std::expected<std::optional<Match>, MatchError> find_first(const ClassFile& file, std::size_t method_index,
                                                           const Pattern& pattern, MatchOptions options) {
  return first_match(search(file, method_index, pattern, options, true));
}
} // namespace cafe::pattern

namespace cafe {

std::expected<std::optional<pattern::Match>, pattern::MatchError> MethodEditor::find_first(
    const pattern::Pattern& query, pattern::MatchOptions options) const {
  return pattern::first_match(pattern::search({storage->pool, storage->version, storage->attribute_entries},
                                              instructions(), code(), query, options, true));
}

std::expected<std::vector<pattern::Match>, pattern::MatchError> MethodEditor::find_all(
    const pattern::Pattern& query, pattern::MatchOptions options) const {
  return pattern::search({storage->pool, storage->version, storage->attribute_entries},
                         instructions(), code(), query, options, false);
}

} // namespace cafe
