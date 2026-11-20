/**
 * @file
 * @brief Convert compact StackMapTable declarations into bounded, instruction-anchored verifier states.
 * @ingroup implementation
 */
#include "ssa_frames.hpp"

#include "analysis_detail.hpp"
#include "cafe/analysis/max_locals.hpp"
#include "instruction_effects.hpp"
#include "io/binary_reader.hpp"
#include "io/code_layout.hpp"
#include "model/budget_resource.hpp"

namespace cafe::analysis {

/// Inspect the publication stamp of caller-supplied Code metadata.
struct DeclaredFrameAccess {
  /// Select the layout associated with the still-current StackMapTable.
  /// @param attribute Decoded Code and its original or supplied metadata stamp.
  /// @return Whether to honor encoding hints, or nullopt for a stale layout.
  static std::optional<bool> encoding(const CodeAttribute& attribute) {
    if (attribute.supplied_metadata) {
      if (attribute.supplied_metadata->revision != attribute.code().revision())
        return std::nullopt;
      return attribute.supplied_metadata->preserve_original_encodings;
    }
    if (attribute.original && !attribute.original_handlers && !attribute.code().edited())
      return true;
    return std::nullopt;
  }
};

/// Allocator, interned names, and frame vectors retained together across moves.
struct DeclaredFrames::Storage {
  /// Start an empty declaration table charged against the supplied policy.
  /// @param limits Allocation resource and memory limit.
  explicit Storage(const AnalysisLimits& limits) :
      budget(limits.upstream_resource, limits.max_memory_bytes), names(&budget), frames(&budget) {
  }

  detail::BudgetResource budget; ///< Bounds working and retained allocations.
  NameSet names; ///< Reference names owned by this table.
  std::pmr::vector<StackMapFrame> frames; ///< Expanded frame records in wire order.
};

DeclaredFrames::DeclaredFrames() = default;
DeclaredFrames::~DeclaredFrames() = default;
DeclaredFrames::DeclaredFrames(DeclaredFrames&&) noexcept = default;
DeclaredFrames& DeclaredFrames::operator=(DeclaredFrames&&) noexcept = default;

namespace {

using enum VerificationKind;
using Status = std::expected<void, AnalysisError>; ///< Successful decode step or a structured failure.

/// Expand frames relative to the previous declaration, starting at the implicit method frame.
class TableDecoder {
public:
  /// Construct a cursor over one current StackMapTable payload.
  /// @param owner Class supplying symbolic identity and the constant pool.
  /// @param method Method declaration used for diagnostics.
  /// @param code Decoded Code with the corresponding declared maxima.
  /// @param bytes Complete raw attribute payload.
  /// @param offsets Byte offsets of current instructions in method order, excluding the end.
  /// @param limits Slot, memory, work, and cancellation policy.
  /// @param result Destination for expanded frames and interned names.
  TableDecoder(const ClassFile& owner, const Method& method, const CodeAttribute& code,
               std::span<const std::byte> bytes, std::span<const std::uint32_t> offsets,
               const AnalysisLimits& limits, DeclaredFrames::Storage& result) :
      owner(owner), code(code), reader(bytes), offsets(offsets), limits(limits), result(result),
      site(method_site(owner, method)), work(limits.max_work) {
  }

  /// Expand compressed frame declarations and validate declared sizes.
  /// @param initial Implicit method frame, including its receiver and parameters.
  /// @return Success, a malformed declaration, or a resource or cancellation failure.
  Status decode(const Frame& initial) {
    const auto count = reader.u2();
    if (!count)
      return malformed("truncated StackMapTable count");
    if (*count > limits.max_frames)
      return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "StackMapTable exceeds the frame limit");
    result.frames.reserve(*count);
    Frame state(&result.budget);
    state.locals = initial.locals;
    while (!state.locals.empty() && state.locals.back().kind == Top)
      state.locals.pop_back();
    // These are working entries in the compact verification_type_info array.
    // A long or double consists of one entry, including for append/chop counts.
    std::erase_if(state.locals, [](const auto& type) { return type.kind == WideContinuation; });
    std::size_t next_offset = 0;
    for (unsigned i = 0; i < *count; ++i) {
      if (limits.cancellation.stop_requested())
        return site.fail(AnalysisErrorKind::Cancelled, "StackMapTable decoding cancelled");
      const auto tag = reader.u1();
      if (!tag || (*tag >= 128 && *tag < 247))
        return malformed("invalid StackMapTable frame tag");
      const auto delta = *tag < 128 ? std::optional<std::uint16_t>(*tag % 64) : reader.u2();
      if (!delta)
        return malformed("truncated StackMapTable frame offset");
      next_offset += *delta;
      auto at = instruction_at(next_offset++);
      if (!at)
        return malformed("declared frame offset is not an instruction boundary");
      state.stack.clear();
      if (*tag >= 248 && *tag <= 250) {
        const auto chop = static_cast<std::size_t>(251 - *tag);
        if (state.locals.size() < chop)
          return malformed("StackMapTable chop exceeds the preceding locals");
        state.locals.resize(state.locals.size() - chop);
      } else if (*tag == 255) {
        state.locals.clear();
        if (auto read = read_types(reader.u2(), state.locals, code.max_locals()); !read)
          return read;
        if (auto read = read_types(reader.u2(), state.stack, code.max_stack()); !read)
          return read;
      } else if (*tag >= 252 && *tag <= 254) {
        if (auto read = read_types(*tag - 251, state.locals, code.max_locals()); !read)
          return read;
      } else if (*tag >= 64 && *tag <= 247) {
        if (auto read = read_types(1, state.stack, code.max_stack()); !read)
          return read;
      }
      const auto cost = 1 + state.locals.size() + state.stack.size();
      if (cost > work)
        return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "StackMapTable expansion exceeds the work limit", *at);
      work -= cost;
      auto& decoded = result.frames.emplace_back(StackMapFrame{*at, Frame{&result.budget}}).frame;
      for (auto value : state.locals) {
        value.reference = intern_name(result.names, value.reference);
        decoded.locals.push_back(value);
        if (value.width() == 2)
          decoded.locals.push_back({WideContinuation});
        decoded.constructor_uninitialized |= value.kind == UninitializedThis;
      }
      for (auto value : state.stack) {
        value.reference = intern_name(result.names, value.reference);
        decoded.stack.push_back(value);
        decoded.stack_slots += value.width();
      }
      if (decoded.locals.size() > code.max_locals() || decoded.stack_slots > code.max_stack())
        return malformed("declared frame exceeds the Code attribute's maxima");
      if (decoded.locals.size() > limits.max_local_slots || decoded.stack_slots > limits.max_stack_slots)
        return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "declared frame exceeds the slot budget", *at);
    }
    if (reader.remaining() != 0)
      return malformed("trailing bytes in StackMapTable");
    return {};
  }

private:
  /// Attach method context to a malformed frame declaration.
  /// @param reason Description of the wire or frame error.
  /// @return The structured analysis failure.
  std::unexpected<AnalysisError> malformed(std::string_view reason) const {
    return site.fail(AnalysisErrorKind::UnrepresentableFrame, reason);
  }

  /// Resolve an exact instruction start, excluding the end of the method.
  /// @param offset Byte offset measured from the start of the code array.
  /// @return A stable instruction handle or nullopt.
  std::optional<InstructionId> instruction_at(std::size_t offset) const {
    auto found = std::ranges::lower_bound(offsets, offset);
    if (found == offsets.end() || *found != offset)
      return std::nullopt;
    return code.code().at(static_cast<std::size_t>(found - offsets.begin()));
  }

  /// Append compact verification_type_info records from the wire cursor.
  /// @param count Wire entry count, or nullopt when its prefix was truncated.
  /// @param types Destination compact local or stack entries.
  /// @param capacity Maximum number of slots declared for this part of the frame.
  /// @return Success or an invalid count, tag, class, or allocation-site failure.
  Status read_types(std::optional<std::uint16_t> count, std::pmr::vector<VerificationType>& types,
                    std::uint16_t capacity) {
    if (!count || types.size() + *count > capacity || *count > reader.remaining())
      return malformed("invalid StackMapTable verification type count");
    for (unsigned i = 0; i < *count; ++i) {
      if ((i % 256) == 0 && limits.cancellation.stop_requested())
        return site.fail(AnalysisErrorKind::Cancelled, "verification type decoding cancelled");
      const auto tag = reader.u1();
      if (!tag)
        return malformed("truncated verification type");
      constexpr VerificationKind KINDS[] = {Top, Integer, Float, Double, Long, Null, UninitializedThis};
      if (*tag < std::size(KINDS)) {
        types.push_back({KINDS[*tag]});
      } else if (*tag == 7) {
        const auto index = reader.u2();
        if (!index)
          return malformed("truncated object verification type");
        const auto* entry = owner.constant_pool().get(CpIndex{*index});
        if (!entry || entry->tag != CpTag::Class)
          return malformed("object verification type does not reference a class constant");
        const auto name = owner.constant_pool().get_utf8(entry->first);
        if (!name)
          return malformed("invalid object verification type name");
        types.push_back({Object, *name});
      } else if (*tag == 8) {
        const auto offset = reader.u2();
        const auto at = offset ? instruction_at(*offset) : std::nullopt;
        if (!at)
          return malformed("invalid uninitialized verification type offset");
        const auto allocation = code.code().get(*at);
        if (!allocation || allocation->opcode() != Opcode::New)
          return malformed("uninitialized verification type must identify a new instruction");
        const auto name = detail::class_name(owner.constant_pool(), allocation->constant_index().value_or(CpIndex{}));
        if (!name)
          return malformed("invalid class at uninitialized verification allocation site");
        types.push_back({Uninitialized, *name, *at});
      } else {
        return malformed("invalid verification type tag");
      }
    }
    return {};
  }

  const ClassFile& owner; ///< Class owning the referenced constants.
  const CodeAttribute& code; ///< Decoded method and declared slot ceilings.
  detail::BinaryReader reader; ///< Cursor limited to the StackMapTable body.
  std::span<const std::uint32_t> offsets; ///< Instruction start offsets in ascending order.
  const AnalysisLimits& limits; ///< Borrowed resource and cancellation policy.
  DeclaredFrames::Storage& result; ///< Owned frame records under construction.
  MethodSite site; ///< Method identity attached to failures.
  std::size_t work; ///< Remaining budget for expansion of repeated declarations.
};

} // namespace

std::expected<DeclaredFrames, AnalysisError> decode_ssa_frames(
    const ClassFile& owner, const Method& method, const AnalysisLimits& limits) try {
  const auto site = method_site(owner, method);
  const auto* attribute = method.code();
  if (!attribute || !owns_method(owner, method))
    return site.fail(AnalysisErrorKind::MissingCode, "declared frames require an owned decoded method");
  if (limits.cancellation.stop_requested())
    return site.fail(AnalysisErrorKind::Cancelled, "StackMapTable decoding cancelled");
  DeclaredFrames result;
  std::optional<std::span<const std::byte>> bytes;
  for (const auto& nested : attribute->attributes()) {
    if (owner.constant_pool().get_utf8(nested.name_index()).value_or("") != "StackMapTable")
      continue;
    if (bytes)
      return site.fail(AnalysisErrorKind::UnrepresentableFrame, "duplicate StackMapTable attribute");
    if (const auto* raw = nested.get<RawAttribute>())
      bytes = raw->bytes(owner);
    if (!bytes)
      return site.fail(AnalysisErrorKind::UnrepresentableFrame, "invalid StackMapTable payload");
  }
  const auto encoding = DeclaredFrameAccess::encoding(*attribute);
  if (!bytes || !encoding)
    return result;
  if (owner.version().major < 50)
    return site.fail(AnalysisErrorKind::UnsupportedClassVersion, "StackMapTable requires major version 50 or later");
  if (bytes->size() > limits.max_work)
    return site.fail(AnalysisErrorKind::ResourceLimitExceeded, "StackMapTable exceeds the decoding work limit");
  auto locals = compute_max_locals(method, owner, limits);
  if (!locals)
    return std::unexpected(std::move(locals.error()));
  if (*locals > attribute->max_locals())
    return site.fail(AnalysisErrorKind::LocalOverflow, "declared max_locals is too small for this method");
  result.storage = std::make_unique<DeclaredFrames::Storage>(limits);
  auto* scratch = &result.storage->budget;
  auto layout = detail::layout_code(attribute->code(), owner.version(), owner.constant_pool(),
                                    attribute->max_locals(), *encoding, scratch, site.name);
  if (!layout)
    return site.fail(AnalysisErrorKind::InvalidCode, layout.error().detail);
  InstructionEffects effects(owner, method, nullptr, scratch);
  Frame initial(scratch);
  if (auto ready = effects.initialize(initial, *locals); !ready)
    return std::unexpected(site.error(std::move(ready.error())));
  const auto offsets = std::span(layout->offsets).first(attribute->code().size());
  TableDecoder decoder(owner, method, *attribute, *bytes, offsets, limits, *result.storage);
  if (auto decoded = decoder.decode(initial); !decoded)
    return std::unexpected(std::move(decoded.error()));
  result.entries = result.storage->frames;
  auto cfg = ssa_cfg(attribute->code(), attribute->exception_handlers(), result.entries, scratch, limits);
  if (!cfg)
    return std::unexpected(site.error(std::move(cfg.error())));
  auto remaining = result.entries;
  for (const auto& block : cfg->blocks()) {
    if (!remaining.empty() && remaining.front().instruction == block.first) {
      remaining = remaining.subspan(1);
    } else if (block.frame_boundary) {
      return site.fail(AnalysisErrorKind::UnrepresentableFrame, "required declared stack map frame is missing", block.first);
    }
  }
  return result;
} catch (const detail::BudgetExceeded&) {
  return method_site(owner, method).fail(AnalysisErrorKind::ResourceLimitExceeded, "StackMapTable exceeds memory budget");
}

} // namespace cafe::analysis
