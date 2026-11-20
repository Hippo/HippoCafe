/**
 * @file
 * @brief Rebuild local lifetimes and type-annotation targets for SSA-emitted code.
 * @ingroup implementation
 */

#include "ssa_relocation.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_map>

#include <cafe/class_file.hpp>
#include <cafe/read_options.hpp>

#include "binary_reader.hpp"
#include "binary_writer.hpp"
#include "bytecode/code_access.hpp"
#include "code_encoder.hpp"
#include "opaque_attribute.hpp"
#include "stack_map_encoder.hpp"

namespace cafe::detail {

/// Inspect the offset coordinate system attached to the retained Code attributes.
struct SsaRelocationAccess {
  /**
   * @brief Construct an owned payload with its checked registry discriminator.
   * @param index Constant-pool UTF-8 name.
   * @param kind Validated attribute name kind.
   * @param bytes Completed attribute body to transfer.
   * @return Owned raw attribute with the recognized kind.
   */
  static Attribute raw(CpIndex index, AttributeKind kind, std::vector<std::byte> bytes) {
    auto attr = Attribute::raw(index, std::move(bytes));
    attr.attribute_kind = kind;
    return attr;
  }

  /**
   * @brief Determine whether a previous SSA commit retained an independent original layout.
   * @param code Source method Code carrying the metadata being relocated.
   * @return True when generated offset anchors survived subsequent code edits.
   */
  static bool has_owned_layout(const CodeAttribute& code) {
    return code.owned_origin_code_size.has_value();
  }

  /**
   * @brief Read original offsets paired with stable instruction identities.
   * @param code Source method Code carrying the metadata being relocated.
   * @return Borrowed span valid through inspection of the source method.
   */
  static auto boundaries(const CodeAttribute& code) {
    return std::span(code.original_boundaries);
  }

  /**
   * @brief Read the original exclusive method-end offset.
   * @param code Source method Code carrying the metadata being relocated.
   * @return Original code-array length when its coordinate system is available.
   */
  static std::optional<std::uint32_t> original_size(const CodeAttribute& code) {
    if (code.owned_origin_code_size)
      return *code.owned_origin_code_size;
    if (code.original)
      return code.original->code.length;
    return {};
  }

  /**
   * @brief Check a source metadata stamp and its encoding policy.
   * @param code Source method Code carrying the metadata being relocated.
   * @return Encoding-hint setting for a current stamp, or an empty optional.
   */
  static std::optional<bool> supplied_encoding(const CodeAttribute& code) {
    if (code.supplied_metadata && code.supplied_metadata->revision == code.code().revision())
      return code.supplied_metadata->preserve_original_encodings;
    return {};
  }

  /**
   * @brief Recover the exception table whose ordinals were recorded in the attribute.
   * @param code Source method Code carrying the metadata being relocated.
   * @return Borrowed original table or the current unmodified table.
   */
  static std::span<const ExceptionHandler> original_handlers(const CodeAttribute& code) {
    if (code.original_handlers)
      return *code.original_handlers;
    return code.exception_handlers();
  }
};

namespace {

/// Success or an actionable failure for one relocation stage.
using Status = std::expected<void, WriteError>;
/// Maximum wire count or offset in metadata tables.
constexpr std::uint16_t MAX_U2 = std::numeric_limits<std::uint16_t>::max();

/**
 * @brief Construct an owned error record for a rejected relocation.
 * @param kind Machine-readable failure classification.
 * @param path Source Code or nested attribute path.
 * @param detail Explanation of the failure.
 * @return Unexpected result with independent message storage.
 */
std::unexpected<WriteError> error(WriteErrorKind kind, std::string_view path, std::string_view detail) {
  return std::unexpected(WriteError{kind, std::string(path), std::string(detail)});
}

/**
 * @brief Pack both components of an instruction identity for map lookup.
 * @param id Method-scoped stable instruction identity.
 * @return Lossless generation and slot key.
 */
std::uint64_t key(InstructionId id) {
  return (std::uint64_t{id.generation} << 32) | id.index;
}

/**
 * @brief Read a u2 from a previously validated attribute record.
 * @param bytes Bounded wire payload.
 * @param at Fixed-width field offset established by the attribute validator.
 * @return Unsigned big-endian value.
 */
std::uint16_t field(std::span<const std::byte> bytes, std::size_t at) {
  return static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[at]) << 8) |
                                    std::to_integer<unsigned>(bytes[at + 1]));
}

/// Half-open byte interval in the destination code array.
struct Extent {
  std::uint16_t begin; ///< Inclusive destination byte offset.
  std::uint16_t end; ///< Exclusive destination byte offset.
};

/// Destination lifetime and local slot for one wire range.
struct LocalRange {
  Extent interval; ///< Destination byte range.
  std::uint16_t slot; ///< Destination JVM local slot.
};

/// Original table coordinates, the current corresponding instruction ordinals, and staged byte intervals.
class Relocator {
public:
  /**
   * @brief Build the staging instruction-offset lookup over borrowed inputs.
   * @param owner Owning class and constant pool.
   * @param source Source method Code and its nested attributes.
   * @param staged Detached instruction sequence.
   * @param encoded Final staged layout including conditional expansion.
   * @param provenance Source locations recorded by the emitter.
   * @param output Final metadata-validation context.
   * @param scratch Temporary allocator for the relocation.
   * @param limits Work, allocation and cancellation bounds.
   * @param path Structural diagnostic location.
   */
  Relocator(const ClassFile& owner, const CodeAttribute& source, const MethodCode& staged, const EncodedCode& encoded,
            const SsaCodeProvenance& provenance, const CodeAttributeContext& output, std::pmr::memory_resource* scratch,
            const analysis::AnalysisLimits& limits, std::string_view path) :
      path(path), owner(owner), source(source), staged(staged), encoded(encoded), provenance(provenance), output(output),
      scratch(scratch), limits(limits), work(limits.max_work), memory(limits.max_memory_bytes),
      input_bytes(scratch), starts(scratch), boundaries(scratch), spans(scratch), mapped_slots(scratch),
      handlers(scratch), offsets(staged, encoded.instruction_offsets(), scratch) {
  }

  /**
   * @brief Resolve the two coordinate systems before processing attribute rows.
   * @return Success or a failed source-layout or exception-row mapping.
   */
  Status initialize();
  /**
   * @brief Validate a bounded source attribute and rebuild and revalidate its destination payload.
   * @param spec Known registry entry for a Code attribute.
   * @param bytes Original length-bounded attribute body.
   * @return Owned relocated payload or a validation or resource failure.
   */
  std::expected<std::vector<std::byte>, WriteError> rewrite(const AttributeSpec& spec, std::span<const std::byte> bytes);
  std::string_view path; ///< Borrowed path used while processing the current source attribute.

private:
  /// Stable identity match for a source byte boundary.
  struct Boundary {
    std::uint32_t ordinal; ///< Instruction order in the current source method.
    bool original_op; ///< Whether the original annotated operation survived raw edits.
  };
  /// Destination extent and optional exact operation for a source ordinal.
  struct Origin {
    Extent extent; ///< Lowered bytecode span for this source instruction.
    std::uint16_t local_begin; ///< Earliest PC in this block covered by the incoming debugger state.
    std::optional<std::uint16_t> exact; ///< Offset of the equivalent operation for instruction annotations.
  };

  /**
   * @brief Charge bounded work and poll cooperative cancellation.
   * @param amount Units of work for the upcoming traversal.
   * @return Success or a resource or cancellation error.
   */
  Status spend(std::size_t amount);
  /**
   * @brief Charge retained destination bytes before growing the output.
   * @param amount Number of bytes about to be emitted.
   * @return Success or an allocation, work, or cancellation error.
   */
  Status write_bytes(std::size_t amount);
  /**
   * @brief Resolve source boundary identities, output extents and local homes.
   * @return Success or a stale, invalid or unencodable layout error.
   */
  Status source_layout();
  /**
   * @brief Map original catch ordinals to their surviving emitted exception-table rows.
   * @return Success or an invalid provenance or resource error.
   */
  Status handler_mapping();
  /**
   * @brief Resolve a source point to an emitted point of the required fidelity.
   * @param pc Original source byte offset.
   * @param exact Require the same semantic operation for an instruction annotation.
   * @return Emitted PC or an unmapped offset error.
   */
  std::expected<std::uint16_t, WriteError> at(std::uint16_t pc, bool exact) const;
  /**
   * @brief Intersect the emitted pieces of an original scope with proven local homes.
   * @param pc Source start byte offset.
   * @param length Source scope length in bytes.
   * @param slot Original physical local slot.
   * @return Ordered destination ranges or a missing or ambiguous mapping.
   */
  std::expected<std::pmr::vector<LocalRange>, WriteError> local_ranges(std::uint16_t pc, std::uint16_t length,
                                                                       std::uint16_t slot);
  /**
   * @brief Emit destination line entries or local-variable entries.
   * @param bytes Validated source payload.
   * @param lines Select line entries or ten-byte local-variable entries.
   * @param writer Destination payload including reserved table count.
   * @param count Accumulated output entry count.
   * @return Success or an offset, count, or resource error.
   */
  Status debug_table(std::span<const std::byte> bytes, bool lines, BinaryWriter<std::vector<std::byte>>& writer,
                     std::size_t& count);
  /**
   * @brief Rewrite validated type-annotation targets and copy their payloads verbatim.
   * @param bytes Validated source payload.
   * @param records Bounded record offsets supplied by the annotation walker.
   * @param writer Destination payload including reserved annotation count.
   * @param count Accumulated output annotation count.
   * @return Success or a target, count, or resource error.
   */
  Status annotations(std::span<const std::byte> bytes, std::span<const std::pair<std::size_t, std::size_t>> records,
                     BinaryWriter<std::vector<std::byte>>& writer, std::size_t& count);

  const ClassFile& owner; ///< Borrowed class and constant pool.
  const CodeAttribute& source; ///< Borrowed method supplying original metadata.
  const MethodCode& staged; ///< Detached logical bytecode before expansion.
  const EncodedCode& encoded; ///< Final wire layout of the staged method.
  const SsaCodeProvenance& provenance; ///< Emitter-supplied instruction, local, and handler relationships.
  const CodeAttributeContext& output; ///< Final metadata validation bounds.
  std::pmr::memory_resource* scratch; ///< Borrowed allocator for scratch maps and intervals.
  const analysis::AnalysisLimits& limits; ///< Borrowed limits and cancellation token.
  std::size_t work; ///< Remaining charged traversal work.
  std::size_t memory; ///< Remaining budget for retained output payloads.
  std::pmr::vector<std::byte> input_bytes; ///< Length of the original code array used to validate debug and annotation offsets.
  std::pmr::vector<std::uint8_t> starts; ///< Original instruction-boundary bitmap for offset validation.
  std::pmr::unordered_map<std::uint32_t, Boundary> boundaries; ///< Original metadata offsets mapped to surviving source ordinals.
  std::pmr::vector<std::optional<Origin>> spans; ///< Lowered intervals in current source instruction order.
  std::pmr::vector<std::pair<std::uint16_t, LocalRange>> mapped_slots; ///< Emitted local homes grouped by original slot.
  std::pmr::vector<std::pair<std::uint16_t, std::uint16_t>> handlers; ///< Ordered pairs of original and emitted clause ordinals.
  InstructionOffsets offsets; ///< Final offsets of staged instructions.
};

Status Relocator::spend(std::size_t amount) {
  if (limits.cancellation.stop_requested())
    return error(WriteErrorKind::Cancelled, path, "Code metadata relocation was cancelled");
  if (amount > work)
    return error(WriteErrorKind::ResourceLimitExceeded, path, "Code metadata relocation exceeded its work limit");
  work -= amount;
  return {};
}

Status Relocator::write_bytes(std::size_t amount) {
  if (auto charged = spend(amount); !charged)
    return charged;
  if (amount > memory)
    return error(WriteErrorKind::ResourceLimitExceeded, path, "relocated Code attributes exceed the memory limit");
  memory -= amount;
  return {};
}

/**
 * @brief Recover the coordinate system of retained attributes, including anchors surviving raw edits.
 * @tparam Boundary Callback accepting a byte offset, source instruction and unchanged flag.
 * @param owner Class containing the method.
 * @param source Source method Code and retained instruction anchors.
 * @param input_bytes Buffer sized to the encoded source method.
 * @param starts Output bitmap of source instruction boundaries used by Code attributes.
 * @param scratch Bounded scratch allocator.
 * @param path Diagnostic path for encoding and boundary errors.
 * @param boundary Receives each mapped source boundary.
 * @return Success or a missing or invalid source layout error.
 */
template<class Boundary>
Status source_coordinates(const ClassFile& owner, const CodeAttribute& source, std::pmr::vector<std::byte>& input_bytes,
                          std::pmr::vector<std::uint8_t>& starts, std::pmr::memory_resource* scratch,
                          std::string_view path, Boundary boundary) {
  const auto& code = source.code();
  const auto original = SsaRelocationAccess::original_size(source);
  const auto supplied = SsaRelocationAccess::supplied_encoding(source);
  // Supplied metadata replaces parsed attributes and has an independent coordinate system.
  if (supplied) {
    auto input = encode_code(code, owner.version(), owner.constant_pool(), source.max_locals(), *supplied, scratch, path);
    if (!input)
      return std::unexpected(std::move(input.error()));
    input_bytes.resize(input->bytes().size());
    starts.assign(input->instruction_starts().begin(), input->instruction_starts().end());
    const auto pcs = input->instruction_offsets();
    for (std::uint32_t i = 0; i < code.size(); ++i)
      boundary(pcs[i], code.at(i), true);
  } else if (original) {
    input_bytes.resize(*original);
    starts.resize(*original);
    for (const auto& record : SsaRelocationAccess::boundaries(source)) {
      if (record.offset >= starts.size())
        return error(WriteErrorKind::InvalidClassModel, path, "original Code boundary is outside code_length");
      starts[record.offset] = 1;
      const auto instruction = code.get(record.instruction);
      if (instruction)
        boundary(record.offset, record.instruction, *instruction == record.original_instruction);
    }
  } else {
    return error(WriteErrorKind::StaleCodeMetadata, path, "source Code metadata has no current offset map");
  }
  return {};
}

Status Relocator::source_layout() {
  const auto& code = source.code();
  if (auto charged = spend(code.size() + provenance.instructions.size()); !charged)
    return charged;
  spans.resize(code.size());
  std::pmr::unordered_map<std::uint64_t, std::uint32_t> ordinals(scratch);
  for (std::uint32_t index = 0; index < code.size(); ++index)
    ordinals.emplace(key(code.at(index)), index);

  const auto extent = [&](LabelId begin, LabelId end) -> std::optional<Extent> {
    const auto a = encoded.offset_of(begin);
    const auto b = encoded.offset_of(end);
    if (!a || !b || *a > *b || *b > MAX_U2)
      return {};
    return Extent{static_cast<std::uint16_t>(*a), static_cast<std::uint16_t>(*b)};
  };
  for (const auto& origin : provenance.instructions) {
    const auto pos = ordinals.find(key(origin.source));
    const auto lowered = extent(origin.begin, origin.end);
    if (pos == ordinals.end() || !lowered || spans[pos->second])
      return error(WriteErrorKind::InvalidCodeLayout, path, "invalid or repeated source instruction provenance");
    std::optional<std::uint16_t> exact;
    if (origin.exact) {
      const auto pc = offsets.offset_of(*origin.exact);
      const auto from = code.get(origin.source);
      const auto to = staged.get(*origin.exact);
      if (!pc || *pc < lowered->begin || *pc >= lowered->end || !from || !to)
        return error(WriteErrorKind::InvalidCodeLayout, path, "exact instruction target is outside its emitted interval");
      auto wire = static_cast<Opcode>(encoded.bytes()[*pc]);
      if (wire == Opcode::Wide && std::size_t{*pc} + 1 < encoded.bytes().size())
        wire = static_cast<Opcode>(encoded.bytes()[std::size_t{*pc} + 1]);
      const bool same_operation = from->opcode() == to->opcode() &&
                                  (from->local_index().has_value() || *from == BytecodeAccess::with_hint(*to, from->encoding_hint()));
      if (same_operation && opcode_info(wire).logical_opcode == to->opcode())
        exact = *pc;
    }
    const auto local_begin = origin.local_begin.scope ? encoded.offset_of(origin.local_begin) : std::optional<std::uint32_t>{lowered->begin};
    if (!local_begin || *local_begin > lowered->begin)
      return error(WriteErrorKind::InvalidCodeLayout, path, "invalid local scope start for source instruction");
    spans[pos->second] = Origin{*lowered, static_cast<std::uint16_t>(*local_begin), exact};
  }

  for (const auto& local : provenance.locals) {
    const auto interval = extent(local.begin, local.end);
    if (!interval || local.target_slot >= output.max_locals)
      return error(WriteErrorKind::InvalidCodeLayout, path, "invalid source local provenance");
    mapped_slots.emplace_back(local.source_slot, LocalRange{*interval, local.target_slot});
  }
  std::ranges::sort(mapped_slots, [](const auto& a, const auto& b) {
    return std::tie(a.first, a.second.interval.begin) < std::tie(b.first, b.second.interval.begin);
  });

  auto layout = source_coordinates(owner, source, input_bytes, starts, scratch, path,
                                   [&](std::uint32_t pc, InstructionId id, bool unchanged) {
                                     const auto found = ordinals.find(key(id));
                                     if (found != ordinals.end())
                                       boundaries.emplace(pc, Boundary{found->second, unchanged});
                                   });
  if (!layout)
    return layout;
  boundaries.emplace(input_bytes.size(), Boundary{static_cast<std::uint32_t>(code.size()), false});
  return {};
}

Status Relocator::handler_mapping() {
  const auto original = SsaRelocationAccess::original_handlers(source);
  const auto current = source.exception_handlers();
  if (original.size() > MAX_U2 || current.size() > MAX_U2 || provenance.handler_sources.size() != output.exception_count)
    return error(WriteErrorKind::InvalidCodeLayout, path, "exception table and its source identities disagree");
  if (auto charged = spend(original.size() + current.size() + provenance.handler_sources.size()); !charged)
    return charged;

  std::pmr::vector<std::optional<std::uint16_t>> old_indices(current.size(), scratch);
  if (std::ranges::equal(original, current)) {
    for (std::size_t i = 0; i < current.size(); ++i)
      old_indices[i] = static_cast<std::uint16_t>(i);
  } else {
    const auto identity = [](const ExceptionHandler& h) {
      return std::tuple{h.start.scope, h.start.value, h.end.scope, h.end.value, h.handler.scope, h.handler.value, h.catch_type.value};
    };
    std::pmr::vector<std::size_t> old_order(original.size(), scratch), current_order(current.size(), scratch);
    std::iota(old_order.begin(), old_order.end(), 0);
    std::iota(current_order.begin(), current_order.end(), 0);
    const auto old_key = [&](std::size_t i) { return identity(original[i]); };
    const auto new_key = [&](std::size_t i) { return identity(current[i]); };
    std::ranges::sort(old_order, {}, old_key);
    std::ranges::sort(current_order, {}, new_key);
    for (const auto i : current_order) {
      const auto match = std::ranges::equal_range(old_order, new_key(i), {}, old_key);
      if (match.size() == 1 && std::ranges::equal_range(current_order, new_key(i), {}, new_key).size() == 1)
        old_indices[i] = static_cast<std::uint16_t>(match.front());
    }
  }
  for (std::size_t i = 0; i < provenance.handler_sources.size(); ++i) {
    const auto from = provenance.handler_sources[i];
    if (!from)
      continue;
    if (*from >= old_indices.size())
      return error(WriteErrorKind::InvalidCodeLayout, path, "lowered handler refers to an unknown source clause");
    if (const auto old = old_indices[*from])
      handlers.emplace_back(*old, static_cast<std::uint16_t>(i));
  }
  std::ranges::sort(handlers);
  return {};
}

Status Relocator::initialize() {
  if (auto status = source_layout(); !status)
    return status;
  return handler_mapping();
}

std::expected<std::uint16_t, WriteError> Relocator::at(std::uint16_t pc, bool exact) const {
  const auto boundary = boundaries.find(pc);
  if (boundary != boundaries.end() && boundary->second.ordinal < spans.size()) {
    if (const auto& origin = spans[boundary->second.ordinal]) {
      if (exact && boundary->second.original_op && origin->exact)
        return *origin->exact;
      if (!exact && origin->extent.begin < output.bytes.size())
        return origin->extent.begin;
    }
  }
  return error(WriteErrorKind::UnmappedCodeOffset, path, "source instruction has no equivalent emitted anchor");
}

std::expected<std::pmr::vector<LocalRange>, WriteError> Relocator::local_ranges(
    std::uint16_t pc, std::uint16_t length, std::uint16_t slot) {
  const auto from = boundaries.find(pc);
  const auto to = boundaries.find(std::uint32_t{pc} + length);
  if (from == boundaries.end() || to == boundaries.end() || from->second.ordinal > to->second.ordinal)
    return error(WriteErrorKind::UnmappedCodeOffset, path, "source local lifetime has a missing boundary");
  if (auto charged = spend(to->second.ordinal - from->second.ordinal + 1); !charged)
    return std::unexpected(std::move(charged.error()));
  std::pmr::vector<Extent> pieces(scratch);
  if (length == 0) {
    auto anchor = at(pc, false);
    if (!anchor)
      return std::unexpected(std::move(anchor.error()));
    pieces.push_back({*anchor, *anchor});
  } else {
    for (auto i = from->second.ordinal; i < to->second.ordinal; ++i)
      if (const auto& origin = spans[i]; origin && origin->local_begin < origin->extent.end)
        pieces.push_back({origin->local_begin, origin->extent.end});
    // A local may be in scope only for a load that vanished through SSA renaming. Its
    // known destination boundary can still describe that now-empty lexical scope.
    if (pieces.empty()) {
      for (auto i = from->second.ordinal; i < to->second.ordinal; ++i) {
        const auto& origin = spans[i];
        if (!origin || origin->extent.begin >= output.bytes.size())
          return error(WriteErrorKind::UnmappedCodeOffset, path, "eliminated local lifetime has no emitted boundary");
        pieces.push_back(origin->extent);
      }
    }
  }
  std::ranges::sort(pieces, {}, &Extent::begin);
  std::pmr::vector<Extent> merged(scratch);
  for (const auto piece : pieces) {
    if (!merged.empty() && piece.begin <= merged.back().end)
      merged.back().end = std::max(merged.back().end, piece.end);
    else
      merged.push_back(piece);
  }

  const auto homes = std::ranges::equal_range(mapped_slots, slot, {}, &decltype(mapped_slots)::value_type::first);
  std::pmr::vector<LocalRange> result(scratch);
  const auto append = [&](Extent part, std::uint16_t home) {
    if (!result.empty() && result.back().slot == home && result.back().interval.end == part.begin)
      result.back().interval.end = part.end;
    else
      result.push_back({part, home});
  };
  for (const auto interval : merged) {
    if (auto charged = spend(homes.size()); !charged)
      return std::unexpected(std::move(charged.error()));
    auto covered = interval.begin;
    bool located = false;
    for (const auto& [unused, home] : homes) {
      static_cast<void>(unused);
      const auto begin = std::max(interval.begin, home.interval.begin);
      const auto end = std::min(interval.end, home.interval.end);
      if ((begin < end || (interval.begin == interval.end && home.interval.begin <= interval.begin && interval.begin < home.interval.end))) {
        if (covered != begin || (interval.begin == interval.end && located))
          return error(WriteErrorKind::UnmappedCodeOffset, path, "local has an ambiguous or incomplete emitted lifetime");
        append({begin, end}, home.slot);
        covered = end;
        located = true;
      }
    }
    if (!located || covered != interval.end)
      return error(WriteErrorKind::UnmappedCodeOffset, path, "local has no proven slot across its emitted lifetime");
  }
  if (result.empty())
    return error(WriteErrorKind::UnmappedCodeOffset, path, "source local lifetime has no emitted instructions");
  return result;
}

Status Relocator::debug_table(std::span<const std::byte> bytes, bool lines, BinaryWriter<std::vector<std::byte>>& writer,
                              std::size_t& count) {
  const std::size_t stride = lines ? 4 : 10;
  for (std::size_t pos = 2; pos < bytes.size(); pos += stride) {
    const auto pc = field(bytes, pos);
    if (lines) {
      const auto target = at(pc, false);
      if (!target)
        return std::unexpected(target.error());
      if (auto allowed = write_bytes(4); !allowed)
        return allowed;
      writer.u2(*target);
      writer.u2(field(bytes, pos + 2));
      ++count;
      continue;
    }
    auto locals = local_ranges(pc, field(bytes, pos + 2), field(bytes, pos + 8));
    if (!locals)
      return std::unexpected(std::move(locals.error()));
    if (locals->size() > MAX_U2 - count)
      return error(WriteErrorKind::AttributeTooLarge, path, "local-variable table exceeds its u2 count");
    if (auto allowed = write_bytes(10 * locals->size()); !allowed)
      return allowed;
    count += locals->size();
    for (const auto& [range, slot] : *locals) {
      writer.u2(range.begin);
      writer.u2(range.end - range.begin);
      writer.bytes(bytes.subspan(pos + 4, 4));
      writer.u2(slot);
    }
  }
  return {};
}

Status Relocator::annotations(std::span<const std::byte> bytes, std::span<const std::pair<std::size_t, std::size_t>> records,
                              BinaryWriter<std::vector<std::byte>>& writer, std::size_t& count) {
  for (const auto& [start, end] : records) {
    const auto record = bytes.subspan(start, end - start);
    const auto target = std::to_integer<std::uint8_t>(record[0]);
    if (target == 0x42) {
      const auto rows = std::ranges::equal_range(handlers, field(record, 1), {}, &decltype(handlers)::value_type::first);
      if (rows.empty())
        return error(WriteErrorKind::UnmappedCodeOffset, path, "annotated catch clause has no unique source or emitted row");
      if (rows.size() > MAX_U2 - count)
        return error(WriteErrorKind::AttributeTooLarge, path, "split catch annotations exceed the u2 count");
      for (const auto& row : rows) {
        if (auto allowed = write_bytes(record.size()); !allowed)
          return allowed;
        writer.u1(target);
        writer.u2(row.second);
        writer.bytes(record.subspan(3));
      }
      count += rows.size();
      continue;
    }
    if (count == MAX_U2)
      return error(WriteErrorKind::AttributeTooLarge, path, "type annotations exceed the u2 count");
    ++count;
    if (auto allowed = write_bytes(3); !allowed)
      return allowed;
    writer.u1(target);
    if (target == 0x40 || target == 0x41) {
      const auto size_pos = writer.size();
      writer.u2(0);
      const auto ranges = field(record, 1);
      std::size_t pos = 3;
      std::size_t written = 0;
      for (std::size_t i = 0; i < ranges; ++i, pos += 6) {
        auto locals = local_ranges(field(record, pos), field(record, pos + 2), field(record, pos + 4));
        if (!locals)
          return std::unexpected(std::move(locals.error()));
        if (locals->size() > MAX_U2 - written)
          return error(WriteErrorKind::AttributeTooLarge, path, "type annotation local ranges exceed the u2 count");
        if (auto allowed = write_bytes(6 * locals->size()); !allowed)
          return allowed;
        written += locals->size();
        for (const auto& [range, slot] : *locals) {
          writer.u2(range.begin);
          writer.u2(range.end - range.begin);
          writer.u2(slot);
        }
      }
      if (!writer.patch_u2(size_pos, static_cast<std::uint16_t>(written)))
        return error(WriteErrorKind::InvalidCodeLayout, path, "cannot finalize annotation local range count");
      if (auto allowed = write_bytes(record.size() - pos); !allowed)
        return allowed;
      writer.bytes(record.subspan(pos));
    } else {
      auto offset = at(field(record, 1), true);
      if (!offset)
        return std::unexpected(std::move(offset.error()));
      if (auto allowed = write_bytes(record.size() - 3); !allowed)
        return allowed;
      writer.u2(*offset);
      writer.bytes(record.subspan(3));
    }
  }
  return {};
}

std::expected<std::vector<std::byte>, WriteError> Relocator::rewrite(const AttributeSpec& spec,
                                                                     std::span<const std::byte> bytes) {
  if (auto allowed = spend(bytes.size()); !allowed)
    return std::unexpected(std::move(allowed.error()));
  const auto old_handlers = SsaRelocationAccess::original_handlers(source);
  const CodeAttributeContext input{input_bytes, starts, source.max_locals(), static_cast<std::uint16_t>(old_handlers.size())};
  using Records = std::pmr::vector<std::pair<std::size_t, std::size_t>>;
  Records records(scratch);
  CodeOffsetVisitor visitor{&records, nullptr, [](void* state, std::size_t begin, std::size_t end) {
                              static_cast<Records*>(state)->emplace_back(begin, end);
                            }};
  BinaryReader original(bytes);
  if (auto checked = validate_opaque_attribute(original, spec, AttributeContext::Code, owner.constant_pool(),
                                               ReadLimits{}, path, &input, scratch, visitor);
      !checked)
    return error(checked.error().kind == ReadErrorKind::ResourceLimitExceeded ? WriteErrorKind::ResourceLimitExceeded
                                                                              : WriteErrorKind::UnmappedCodeOffset,
                 path, checked.error().detail);

  std::vector<std::byte> buffer;
  BinaryWriter writer(buffer);
  if (auto allowed = write_bytes(2); !allowed)
    return std::unexpected(std::move(allowed.error()));
  writer.u2(0);
  std::size_t count = 0;
  Status status;
  switch (spec.kind) {
    case AttributeKind::LineNumberTable:
    case AttributeKind::LocalVariableTable:
    case AttributeKind::LocalVariableTypeTable:
      status = debug_table(bytes, spec.kind == AttributeKind::LineNumberTable, writer, count);
      break;
    case AttributeKind::RuntimeVisibleTypeAnnotations:
    case AttributeKind::RuntimeInvisibleTypeAnnotations:
      status = annotations(bytes, records, writer, count);
      break;
    default:
      return error(WriteErrorKind::UnsafeOpaqueAttribute, path, "Code attribute has no SSA relocation contract");
  }
  if (!status)
    return std::unexpected(std::move(status.error()));
  if (!writer.patch_u2(0, static_cast<std::uint16_t>(count)) || writer.size() > std::numeric_limits<std::uint32_t>::max())
    return error(WriteErrorKind::AttributeTooLarge, path, "relocated attribute exceeds the class-file length");
  BinaryReader result(buffer);
  if (auto checked = validate_opaque_attribute(result, spec, AttributeContext::Code, owner.constant_pool(),
                                               ReadLimits{}, path, &output, scratch);
      !checked)
    return error(checked.error().kind == ReadErrorKind::ResourceLimitExceeded ? WriteErrorKind::ResourceLimitExceeded
                                                                              : WriteErrorKind::UnmappedCodeOffset,
                 path, checked.error().detail);
  return buffer;
}

} // namespace

std::expected<std::pmr::vector<std::uint8_t>, WriteError> ssa_debug_slot_widths(
    const ClassFile& owner, const CodeAttribute& source, const analysis::AnalysisLimits& limits,
    std::size_t& remaining, std::pmr::memory_resource* scratch, std::pmr::vector<std::uint8_t>* anchors) {
  std::pmr::vector<std::uint8_t> widths(scratch);
  std::pmr::vector<std::byte> code_bytes(scratch);
  std::pmr::vector<std::uint8_t> starts(scratch);
  std::pmr::unordered_map<std::uint32_t, InstructionId> identities(scratch);
  const auto anchor = [anchors](InstructionId id) {
    if (anchors) {
      if (anchors->size() <= id.index)
        anchors->resize(std::size_t{id.index} + 1);
      (*anchors)[id.index] = 1;
    }
  };
  bool unrelocatable = false;
  for (const auto& attribute : source.attributes()) {
    const auto name = owner.constant_pool().get_utf8(attribute.name_index());
    const auto* spec = name ? find_attribute(*name) : nullptr;
    if (!spec) {
      unrelocatable = true;
      continue;
    }
    if (spec->kind != AttributeKind::LocalVariableTable && spec->kind != AttributeKind::LocalVariableTypeTable &&
        spec->kind != AttributeKind::RuntimeVisibleTypeAnnotations && spec->kind != AttributeKind::RuntimeInvisibleTypeAnnotations &&
        !(anchors && spec->kind == AttributeKind::LineNumberTable))
      continue;
    const auto* raw = attribute.get<RawAttribute>();
    if (!raw)
      continue;
    const auto bytes = raw->bytes(owner);
    if (!bytes)
      continue;
    if (limits.cancellation.stop_requested())
      return error(WriteErrorKind::Cancelled, *name, "SSA local slot discovery cancelled");
    const auto layout_work = code_bytes.empty() ? source.code().size() : 0;
    if (bytes->size() > remaining || layout_work > remaining - bytes->size())
      return error(WriteErrorKind::ResourceLimitExceeded, *name, "SSA local slot discovery exceeds the work limit");
    remaining -= bytes->size() + layout_work;
    if (code_bytes.empty()) {
      auto layout = source_coordinates(owner, source, code_bytes, starts, scratch, *name,
                                       [&](std::uint32_t pc, InstructionId id, bool) {
                                         if (anchors)
                                           identities.emplace(pc, id);
                                       });
      if (!layout) {
        if (layout.error().kind == WriteErrorKind::ResourceLimitExceeded)
          return std::unexpected(std::move(layout.error()));
        unrelocatable = true;
        // Metadata with stale coordinates is handled by the final relocation policy.
        break;
      }
    }
    const CodeAttributeContext input{code_bytes, starts, source.max_locals(),
                                     static_cast<std::uint16_t>(SsaRelocationAccess::original_handlers(source).size())};
    const auto mark_offset = [&](std::uint32_t offset) {
      if (auto found = identities.find(offset); found != identities.end())
        anchor(found->second);
    };
    struct VisitorState {
      std::pmr::vector<std::uint8_t>& widths;
      decltype(mark_offset)& mark;
    } state{widths, mark_offset};
    CodeOffsetVisitor visitor{.state = &state};
    visitor.visit = [](void* data, CodeOffsetField& field) {
      const auto& mark = static_cast<VisitorState*>(data)->mark;
      if (field.kind != CodeOffsetField::Kind::Handler)
        mark(field.value);
      if (field.kind == CodeOffsetField::Kind::Range)
        mark(std::uint32_t{field.value} + field.length);
      return true;
    };
    visitor.local_slot = [](void* data, std::uint16_t slot, std::uint16_t width) {
      auto& slots = static_cast<VisitorState*>(data)->widths;
      if (slots.size() <= slot)
        slots.resize(std::size_t{slot} + 1);
      slots[slot] = std::max(slots[slot], static_cast<std::uint8_t>(width));
    };
    BinaryReader reader(*bytes);
    const auto checked = validate_opaque_attribute(reader, *spec, AttributeContext::Code, owner.constant_pool(),
                                                   ReadLimits{}, *name, &input, scratch, visitor);
    if (!checked && checked.error().kind == ReadErrorKind::ResourceLimitExceeded)
      return error(WriteErrorKind::ResourceLimitExceeded, *name, checked.error().detail);
    unrelocatable |= !checked;
  }
  if (unrelocatable && anchors) {
    if (source.code().size() > remaining)
      return error(WriteErrorKind::ResourceLimitExceeded, "Code", "SSA metadata anchoring exceeds the work limit");
    remaining -= source.code().size();
    for (const auto& entry : source.code().instructions())
      anchor(entry.id);
  }
  return widths;
}

std::expected<std::vector<Attribute>, WriteError> relocate_ssa_attributes(
    const ClassFile& owner, const CodeAttribute& source, const MethodCode& staged, const EncodedCode& encoded,
    const SsaCodeProvenance& provenance, const CodeAttributeContext& output_context, bool drop_unsafe,
    std::vector<Diagnostic>& diagnostics, std::pmr::memory_resource* scratch, std::string_view path,
    const analysis::AnalysisLimits& limits, Attribute* stack_map) {
  std::pmr::unsynchronized_pool_resource storage(scratch);
  std::optional<Relocator> relocator;
  Status mapping;
  std::vector<Attribute> result;
  std::vector<Diagnostic> dropped;
  std::size_t index = 0;
  for (const auto& attribute : source.attributes()) {
    const auto name = owner.constant_pool().get_utf8(attribute.name_index());
    const auto label = std::string(path) + ".attributes[" + std::to_string(index++) + "]";
    if (!name)
      return error(WriteErrorKind::InvalidConstantPoolReference, label, "Code attribute name is not UTF-8");
    const auto attr_path = label + "." + std::string(*name);
    const auto* spec = find_attribute(*name);
    if (spec && spec->kind == AttributeKind::StackMapTable) {
      if (stack_map)
        result.push_back(std::move(*std::exchange(stack_map, nullptr)));
      continue;
    }
    const auto* raw = attribute.get<RawAttribute>();
    if (!raw)
      return error(WriteErrorKind::InvalidClassModel, attr_path, "expected a raw nested Code attribute");
    const auto bytes = raw->bytes(owner);
    if (!bytes)
      return error(WriteErrorKind::InvalidClassModel, attr_path, "Code attribute is outside the source class");

    const auto rewrite = [&]() -> std::expected<std::vector<std::byte>, WriteError> {
      if (!spec)
        return error(WriteErrorKind::UnsafeOpaqueAttribute, attr_path, "opaque Code attribute may contain offsets or local slots");
      if (!spec->applicable_to(owner.version(), AttributeContext::Code))
        return error(WriteErrorKind::InvalidClassModel, attr_path, "attribute is inapplicable to Code");
      if (!raw->source_slice() && !SsaRelocationAccess::supplied_encoding(source) && !SsaRelocationAccess::has_owned_layout(source))
        return error(WriteErrorKind::StaleCodeMetadata, attr_path, "owned metadata no longer matches the source encoding");
      if (!relocator) {
        relocator.emplace(owner, source, staged, encoded, provenance, output_context, &storage, limits, path);
        mapping = relocator->initialize();
      }
      if (!mapping)
        return error(mapping.error().kind, attr_path, mapping.error().detail);
      relocator->path = attr_path;
      return relocator->rewrite(*spec, *bytes);
    };
    auto relocated = rewrite();
    if (!relocated) {
      const auto& failure = relocated.error();
      if (!drop_unsafe || (failure.kind != WriteErrorKind::UnmappedCodeOffset &&
                           failure.kind != WriteErrorKind::StaleCodeMetadata &&
                           failure.kind != WriteErrorKind::UnsafeOpaqueAttribute))
        return std::unexpected(std::move(relocated.error()));
      dropped.push_back({Severity::Warning, DiagnosticCode::DroppedUnsafeCodeAttribute, 0,
                         failure.path, failure.detail, RecoveryAction::Dropped});
      continue;
    }
    result.push_back(SsaRelocationAccess::raw(attribute.name_index(), spec->kind, std::move(*relocated)));
  }
  if (stack_map)
    result.push_back(std::move(*stack_map));
  if (result.size() > MAX_U2)
    return error(WriteErrorKind::AttributeTooLarge, path, "too many nested Code attributes");
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(dropped.begin()), std::make_move_iterator(dropped.end()));
  return result;
}

} // namespace cafe::detail
