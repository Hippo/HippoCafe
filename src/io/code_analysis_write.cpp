/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include "class_writer_impl.hpp"

#include <utility>

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

#include <cafe/analysis/frame_analyzer.hpp>
#include <cafe/analysis/max_locals.hpp>
#include <cafe/analysis/max_stack.hpp>
#include <cafe/read_options.hpp>

#include "analysis/ssa_frames.hpp"
#include "binary_reader.hpp"
#include "class_format.hpp"
#include "code_decoder.hpp"
#include "code_encoder.hpp"

namespace cafe::detail {
namespace {

constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max(); ///< Largest u2 table count accepted by the class-file format.
constexpr auto NO_HANDLER = std::numeric_limits<std::uint32_t>::max(); ///< Sentinel for an original handler ordinal that cannot be uniquely remapped.

/**
 * @brief Translate analysis failure classifications into writer failure classifications.
 * @param error Underlying structured failure to translate or enrich.
 * @param has_context Whether frame computation has an explicit hierarchy context.
 * @return The corresponding WriteErrorKind.
 * @ingroup implementation
 */
WriteErrorKind analysis_error_kind(const analysis::AnalysisError& error, bool has_context) {
  using enum analysis::AnalysisErrorKind;
  switch (error.kind) {
    case UnsupportedClassVersion:
      return WriteErrorKind::UnsupportedDialect;
    case UnsupportedSubroutineAnalysis:
      return WriteErrorKind::UnsupportedSubroutineAnalysis;
    case InvalidConstantPoolReference:
      return WriteErrorKind::InvalidConstantPoolReference;
    case InvalidLabelOrRange:
      return WriteErrorKind::InvalidLabel;
    case Cancelled:
      return WriteErrorKind::Cancelled;
    case ResourceLimitExceeded:
      return WriteErrorKind::ResourceLimitExceeded;
    case TypeResolutionFailure:
      if (!has_context && error.resolution && error.resolution->kind == classpath::ResolutionErrorKind::NotFound)
        return WriteErrorKind::MissingAnalysisContext;
      return WriteErrorKind::ClassResolutionFailure;
    case StackUnderflow:
    case InvalidCategoryOperation:
    case IncompatibleJoin:
    case StackOverflow:
      return WriteErrorKind::InvalidStackShape;
    case AmbiguousTypeJoin:
    case UnrepresentableFrame:
    case UnreachableCode:
      return WriteErrorKind::UnrepresentableFrame;
    case TypeMismatch:
    case UninitializedUse:
    case InvalidConstructorState:
      return WriteErrorKind::InvalidVerificationFrame;
    case InvalidCode:
      return WriteErrorKind::InvalidInstruction;
    case LocalOverflow:
      return WriteErrorKind::InvalidCodeLayout;
    case InvalidDescriptor:
    case MissingCode:
      return WriteErrorKind::InvalidClassModel;
  }
  return WriteErrorKind::InvalidVerificationFrame;
}

} // namespace

/**
 * @brief Attach writer location and byte offsets to an analysis failure.
 * @param error Underlying structured failure to translate or enrich.
 * @param path Borrowed structural path included in diagnostics.
 * @param has_context Whether frame computation has an explicit hierarchy context.
 * @param code Decoded instruction sequence or associated Code attribute.
 * @param offsets Instruction-order byte offsets for the selected code layout.
 * @return An unexpected WriteError preserving the useful underlying explanation.
 * @ingroup implementation
 */
std::unexpected<WriteError> analysis_error(
    const analysis::AnalysisError& error, std::string_view path, bool has_context,
    const MethodCode& code, std::span<const std::uint32_t> offsets) {
  std::string detail = error.class_name + "." + error.method_name + error.descriptor;
  if (error.instruction) {
    std::size_t index = 0;
    for (const auto entry : code.instructions()) {
      if (entry.id == *error.instruction) {
        detail += index < offsets.size() ? " at bytecode offset " + std::to_string(offsets[index])
                                         : " at instruction " + std::to_string(index);
        break;
      }
      ++index;
    }
  }
  if (error.label)
    detail += " at label " + std::to_string(error.label->value);
  detail += ": " + error.detail;
  if (error.resolution) {
    const auto& cause = *error.resolution;
    detail += " (" + cause.name + ": " + cause.detail;
    if (!cause.path.empty())
      detail += " in " + cause.path.string();
    detail += ")";
  }
  return std::unexpected(WriteError{analysis_error_kind(error, has_context), std::string(path), std::move(detail)});
}

namespace {

/**
 * @brief Check whether decoded Code has control-flow boundaries requiring modern frames.
 * @param method Code attribute whose branches and exception handlers are inspected.
 * @return True when stack-map generation is required by the method structure.
 * @ingroup implementation
 */
bool needs_stack_map(const CodeAttribute& method) {
  if (!method.exception_handlers().empty())
    return true;
  std::size_t position = 0;
  for (const auto entry : method.code().instructions()) {
    ++position;
    switch (opcode_info(entry.instruction.opcode()).control_flow) {
      case ControlFlow::ConditionalBranch:
      case ControlFlow::Jump:
      case ControlFlow::Switch:
      case ControlFlow::SubroutineCall:
      case ControlFlow::SubroutineReturn:
        return true;
      case ControlFlow::Return:
      case ControlFlow::Throw:
        if (position < method.code().size())
          return true;
        break;
      case ControlFlow::Next:
        break;
    }
  }
  return false;
}

/**
 * @brief Form a stable comparison key for an exception-table entry.
 * @param handler Exception-table entry whose labels or type are resolved.
 * @return A tuple of scoped range, destination, and catch-type identities.
 * @ingroup implementation
 */
auto handler_key(const ExceptionHandler& handler) {
  return std::tie(handler.start.scope, handler.start.value, handler.end.scope, handler.end.value,
                  handler.handler.scope, handler.handler.value, handler.catch_type.value);
}

/**
 * @brief Match original handler ordinals to unchanged current entries without guessing ambiguous
 * duplicates.
 * @param original Original exception table whose labels are remapped to the emission plan.
 * @param current Current exception table after edits.
 * @param scratch Temporary allocator that must outlive any returned allocator-backed views.
 * @return A scratch-owned old-to-new ordinal mapping; unmappable entries carry the invalid
 *   sentinel.
 * @ingroup implementation
 *
 * A catch annotation refers to a table ordinal, independent of byte offsets.
 * A replaced or ambiguous entry cannot inherit an annotation implicitly.
 */
std::pmr::vector<std::uint32_t> remap_handlers(
    std::span<const ExceptionHandler> original, std::span<const ExceptionHandler> current,
    std::pmr::memory_resource* scratch) {
  std::pmr::vector<std::uint32_t> remapped(original.size(), NO_HANDLER, scratch);
  if (std::ranges::equal(original, current)) {
    std::iota(remapped.begin(), remapped.end(), 0u);
    return remapped;
  }
  std::pmr::vector<std::uint32_t> previous(original.size(), scratch);
  std::pmr::vector<std::uint32_t> ordered(current.size(), scratch);
  std::iota(previous.begin(), previous.end(), 0u);
  std::iota(ordered.begin(), ordered.end(), 0u);
  const auto original_key_at = [&](std::uint32_t index) { return handler_key(original[index]); };
  const auto key_at = [&](std::uint32_t index) { return handler_key(current[index]); };
  std::ranges::sort(previous, {}, original_key_at);
  std::ranges::sort(ordered, {}, key_at);
  for (std::size_t index = 0; index < original.size(); ++index) {
    const auto key = handler_key(original[index]);
    const auto matches = std::ranges::equal_range(ordered, key, {}, key_at);
    if (matches.size() == 1 && std::ranges::equal_range(previous, key, {}, original_key_at).size() == 1)
      remapped[index] = matches.front();
  }
  return remapped;
}

} // namespace

std::expected<void, WriteError> materialize_expanded_code(
    MethodCode& expanded, const MethodCode& original, const EncodedCode& encoded, ClassVersion version,
    const ConstantPool& pool, std::uint16_t max_locals, std::span<const ExceptionHandler> handlers,
    std::span<const analysis::StackMapFrame> seeds, std::pmr::vector<ExceptionHandler>& expanded_handlers,
    std::pmr::vector<std::uint32_t>& expanded_offsets, std::pmr::vector<analysis::StackMapFrame>& expanded_seeds,
    std::pmr::memory_resource* scratch, std::string_view path) {
  auto decoded = decode_code(expanded, encoded.bytes(), 0, version, pool, max_locals, {}, scratch, path);
  if (!decoded)
    return write_error(WriteErrorKind::InvalidCodeLayout, path, decoded.error().detail);
  expanded_offsets.reserve(expanded.size());
  for (std::uint32_t offset = 0; offset < encoded.bytes().size(); ++offset)
    if (decoded->is_instruction_start(offset))
      expanded_offsets.push_back(offset);
  if (handlers.size() > MAX_COUNT)
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "exception table count does not fit u2");
  expanded_handlers.reserve(handlers.size());
  for (const auto& handler : handlers) {
    const auto label = [&](LabelId source) -> std::expected<LabelId, CodeEditError> {
      const auto offset = encoded.offset_of(source);
      if (offset)
        return decoded->label_at(expanded, *offset);
      return std::unexpected(CodeEditError::InvalidLabel);
    };
    const auto start = label(handler.start), end = label(handler.end), target = label(handler.handler);
    if (!start || !end || !target)
      return write_error(WriteErrorKind::InvalidLabel, path, "could not remap handler to expanded code");
    expanded_handlers.push_back({*start, *end, *target, handler.catch_type});
  }
  if (seeds.empty())
    return {};
  const InstructionOffsets old_offsets(original, encoded.instruction_offsets(), scratch);
  const auto remap = [&](InstructionId source) -> std::optional<InstructionId> {
    const auto offset = old_offsets.offset_of(source);
    if (!offset)
      return {};
    const auto match = std::ranges::lower_bound(expanded_offsets, *offset);
    if (match == expanded_offsets.end() || *match != *offset)
      return {};
    return expanded.at(static_cast<std::size_t>(match - expanded_offsets.begin()));
  };
  expanded_seeds.reserve(seeds.size());
  for (const auto& seed : seeds) {
    const auto target = remap(seed.instruction);
    if (!target)
      return write_error(WriteErrorKind::UnrepresentableFrame, path, "verifier root lost its instruction");
    auto& frame = expanded_seeds.emplace_back(analysis::StackMapFrame{*target, analysis::Frame(scratch)}).frame;
    frame.stack_slots = seed.frame.stack_slots;
    frame.constructor_uninitialized = seed.frame.constructor_uninitialized;
    frame.locals = seed.frame.locals;
    frame.stack = seed.frame.stack;
    for (auto* values : {&frame.locals, &frame.stack})
      for (auto& value : *values)
        if (value.kind == analysis::VerificationKind::Uninitialized) {
          const auto mapped = remap(value.allocation);
          if (!mapped)
            return write_error(WriteErrorKind::UnrepresentableFrame, path, "verifier root lost its new instruction");
          value.allocation = *mapped;
        }
  }
  return {};
}

std::expected<void, WriteError> ClassWriter::write_analyzed_code(
    const CodeAttribute& code, const Method& method, std::string_view path) {
  const bool has_context = analysis_context != nullptr;
  const analysis::AnalysisLimits default_limits;
  const auto& limits = has_context ? analysis_context->limits() : default_limits;
  const bool frames = options.code_analysis == CodeAnalysisMode::ComputeFrames;
  const auto& instructions = code.instructions;
  const auto needed_locals = analysis::compute_max_locals(method, model, limits);
  if (!needed_locals)
    return analysis_error(needed_locals.error(), path, has_context, instructions);
  auto max_locals = *needed_locals;
  std::uint16_t max_stack = 0;
  std::pmr::monotonic_buffer_resource scratch(&analysis_budget);
  // CP extension for frames is append-only, so these choices of ldc width
  // remain valid when the staged pool entries are committed.
  auto encoded = encode_code(instructions, version, pool, max_locals,
                             options.preserve_original_encodings, &scratch, path, frames);
  if (!encoded)
    return std::unexpected(std::move(encoded.error()));
  const auto original_bytes = code.original ? model.source_slice(code.original->code) : std::nullopt;
  const bool rewritten = !original_bytes || instructions.edited() || code.original_handlers || code.supplied_metadata ||
                         !std::ranges::equal(*original_bytes, encoded->bytes());

  CpIndex stack_map_name;
  std::pmr::vector<std::byte> stack_map(&scratch);
  if (!frames) {
    const auto needed_stack = analysis::compute_max_stack(model, method, limits);
    if (!needed_stack)
      return analysis_error(needed_stack.error(), path, has_context, instructions, encoded->instruction_offsets());
    max_stack = *needed_stack;
    const bool has_table = std::ranges::any_of(code.attributes(), [&](const Attribute& attribute) {
      const auto name = pool.get_utf8(attribute.name_index());
      return name && *name == "StackMapTable";
    });
    if (version.major >= 50) {
      if ((has_table && rewritten) || (!has_table && needs_stack_map(code)))
        return write_error(WriteErrorKind::NeedsFrameComputation, path, "this method requires new stack-map frames");
      if (has_table) {
        // Existing declared frames may describe additional slots (including
        // unreachable paths). Preserve their bounds while updating maxima.
        max_stack = std::max(max_stack, code.max_stack());
        max_locals = std::max(max_locals, code.max_locals());
      }
    }
  } else {
    if (!has_context && !default_analysis_context)
      default_analysis_context.emplace();
    auto& context = has_context ? *analysis_context : *default_analysis_context;
    // Most methods retain their original logical instructions. If a conditional
    // expanded, verify the final stream including the synthetic goto_w and its
    // protected-range membership before materializing any stack-map offsets.
    std::optional<MethodCode> expanded;
    std::pmr::vector<ExceptionHandler> expanded_handlers(&scratch);
    std::pmr::vector<std::uint32_t> expanded_offsets(&scratch);
    std::pmr::vector<analysis::StackMapFrame> expanded_seeds(&scratch);
    analysis::DeclaredFrames declared;
    if (code.supplied_metadata && code.supplied_metadata->verified &&
        code.supplied_metadata->revision == instructions.revision()) {
      auto decoded = analysis::decode_ssa_frames(model, method, limits);
      if (!decoded)
        return analysis_error(decoded.error(), path, has_context, instructions);
      declared = std::move(*decoded);
    }
    auto seeds = declared.entries;
    std::span<const std::uint32_t> offsets = encoded->instruction_offsets();
    auto handlers = code.exception_handlers();
    if (encoded->has_expanded_conditionals()) {
      expanded.emplace(&scratch);
      if (auto result = materialize_expanded_code(*expanded, instructions, *encoded, version, pool, max_locals,
                                                  handlers, seeds, expanded_handlers, expanded_offsets, expanded_seeds,
                                                  &scratch, path);
          !result)
        return result;
      offsets = expanded_offsets;
      handlers = expanded_handlers;
      seeds = expanded_seeds;
    }

    const auto& emitted = expanded ? *expanded : instructions;
    const auto computed = seeds.empty() ? analysis::compute_frames(model, method, context, emitted, handlers)
                                        : analysis::emission_frames(model, method, emitted, handlers, context, seeds);
    if (!computed)
      return analysis_error(computed.error(), path, has_context, emitted, offsets);
    if (!computed->is_current(emitted, handlers))
      return write_error(WriteErrorKind::StaleAnalysisResult, path, "computed frames do not describe the emitted method");
    max_stack = computed->max_stack();
    max_locals = computed->max_locals();
    if (code.owned_origin_code_size)
      max_locals = std::max(max_locals, code.max_locals());
    if (version.major >= 50 && !computed->frames().empty()) {
      const InstructionOffsets instruction_offsets(emitted, offsets, &scratch);
      auto table = encode_stack_map(*computed, emitted, instruction_offsets, stack_map_constants, &scratch, path);
      if (!table)
        return std::unexpected(std::move(table.error()));
      auto name = stack_map_constants.utf8("StackMapTable", path);
      if (!name)
        return std::unexpected(std::move(name.error()));
      stack_map_name = *name;
      stack_map = std::move(*table);
    }
  }

  auto context = write_code_array(code, *encoded, max_stack, max_locals, path);
  if (!context)
    return std::unexpected(std::move(context.error()));
  return write_analyzed_code_attributes(code, *encoded, *context, stack_map_name, stack_map, rewritten, &scratch, path);
}

std::expected<void, WriteError> ClassWriter::write_analyzed_code_attributes(
    const CodeAttribute& code, const EncodedCode& encoded, const CodeAttributeContext& context,
    CpIndex stack_map_name, std::span<const std::byte> stack_map, bool rewritten,
    std::pmr::memory_resource* scratch, std::string_view path) {
  const bool replace_frames = options.code_analysis == CodeAnalysisMode::ComputeFrames;
  const auto count_offset = output.size();
  output.u2(0);
  std::size_t count = 0;
  std::array<bool, static_cast<std::size_t>(AttributeKind::PermittedSubclasses) + 1> seen{};
  std::optional<InstructionOffsets> instruction_offsets;
  if (rewritten && !code.original_boundaries.empty())
    instruction_offsets.emplace(code.instructions, encoded.instruction_offsets(), scratch);
  const auto original_size = code.original ? code.original->code.length : code.owned_origin_code_size.value_or(0);
  auto handler_offsets = code.original_handlers ? remap_handlers(*code.original_handlers, code.handlers, scratch)
                                                : std::pmr::vector<std::uint32_t>(scratch);
  const bool supplied = code.supplied_metadata && code.supplied_metadata->revision == code.instructions.revision() &&
                        code.supplied_metadata->preserve_original_encodings == options.preserve_original_encodings &&
                        !encoded.has_expanded_conditionals();
  const auto emit = [&](CpIndex name, std::span<const std::byte> bytes) -> std::expected<void, WriteError> {
    if (count >= MAX_COUNT || bytes.size() > std::numeric_limits<std::uint32_t>::max())
      return write_error(WriteErrorKind::AttributeTooLarge, path, "Code attribute count or payload exceeds the class-file limit");
    ++count;
    output.u2(name.value);
    output.u4(static_cast<std::uint32_t>(bytes.size()));
    output.bytes(bytes);
    return {};
  };

  struct Relocation {
    const CodeAttribute& code;
    const EncodedCode& encoded;
    const std::optional<InstructionOffsets>& instructions;
    const std::pmr::vector<std::uint32_t>& handlers;
    std::span<std::byte> output;
    std::uint32_t original_size;

    [[nodiscard]] std::optional<std::uint16_t> at(std::uint32_t offset, bool annotation) const {
      const auto match = std::ranges::lower_bound(code.original_boundaries, offset, {}, &CodeAttribute::OriginalBoundary::offset);
      if (match == code.original_boundaries.end() || match->offset != offset || !instructions)
        return std::nullopt;
      const auto relocated = instructions->offset_of(match->instruction);
      if (annotation) {
        const auto instruction = code.instructions.get(match->instruction);
        if (!instruction || *instruction != match->original_instruction)
          return std::nullopt;
        if (relocated && opcode_info(instruction->opcode()).control_flow == ControlFlow::ConditionalBranch) {
          const auto wire_opcode = static_cast<Opcode>(encoded.bytes()[*relocated]);
          if (opcode_info(wire_opcode).logical_opcode != instruction->opcode())
            return std::nullopt;
        }
      }
      return relocated;
    }

    void u2(std::size_t position, std::uint16_t value) const {
      // Positions of both fields are produced by the bounded attribute reader.
      output[position] = static_cast<std::byte>(value >> 8);
      output[position + 1] = static_cast<std::byte>(value & 0xff);
    }

    bool visit(CodeOffsetField& field) const {
      using enum CodeOffsetField::Kind;
      if (field.kind == Handler) {
        if (code.original_handlers) {
          if (field.value >= handlers.size() || handlers[field.value] == NO_HANDLER)
            return false;
          field.value = static_cast<std::uint16_t>(handlers[field.value]);
        }
      } else {
        const auto start = at(field.value, field.kind == InstructionAnnotation);
        if (!start)
          return false;
        if (field.kind == Range) {
          const auto old_end = std::uint32_t(field.value) + field.length;
          const auto end = old_end == original_size
                               ? std::optional{static_cast<std::uint16_t>(encoded.bytes().size())}
                               : at(old_end, false);
          if (!end || *end < *start)
            return false;
          field.length = static_cast<std::uint16_t>(*end - *start);
          u2(field.position + 2, field.length);
        }
        field.value = *start;
      }
      u2(field.position, field.value);
      return true;
    }
  };

  std::pmr::unsynchronized_pool_resource attribute_scratch(&analysis_budget);
  std::size_t index = 0;
  for (const auto& attribute : code.attributes()) {
    auto attribute_path = std::string(path) + "." + index_path("attributes", index++);
    const auto name = pool.get_utf8(attribute.name_index());
    if (!name)
      return write_error(WriteErrorKind::InvalidConstantPoolReference, attribute_path, "attribute name must refer to valid Utf8");
    const auto* spec = find_attribute(*name);
    if (spec) {
      attribute_path += ".";
      attribute_path += spec->name;
      auto& already_seen = seen[static_cast<std::size_t>(spec->kind)];
      if (already_seen && !spec->repeatable)
        return write_error(WriteErrorKind::InvalidClassModel, attribute_path, "duplicate Code attribute");
      already_seen = true;
      if (replace_frames && spec->kind == AttributeKind::StackMapTable) {
        if (stack_map_name) {
          if (auto written = emit(attribute.name_index(), stack_map); !written)
            return written;
          stack_map_name = {};
        }
        continue;
      }
      if (!spec->applicable_to(version, AttributeContext::Code))
        return write_error(WriteErrorKind::InvalidClassModel, attribute_path, "attribute does not apply to the declared Code version");
    }
    const auto* raw = attribute.get<RawAttribute>();
    const auto bytes = raw ? raw->bytes(model) : std::nullopt;
    if (!bytes)
      return write_error(WriteErrorKind::InvalidClassModel, attribute_path, "expected an in-bounds raw Code attribute");

    const auto unsafe = [&](WriteErrorKind kind, std::string_view reason) -> std::expected<void, WriteError> {
      if (!options.drop_unsafe_code_attributes)
        return write_error(kind, attribute_path, reason);
      result.diagnostics.push_back({Severity::Warning, DiagnosticCode::DroppedUnsafeCodeAttribute,
                                    output.size(), attribute_path, std::string(reason), RecoveryAction::Dropped});
      return {};
    };
    const bool anchored = raw->source_slice() || code.owned_origin_code_size.has_value();
    if (rewritten && (!spec || (!anchored && !supplied))) {
      if (auto handled = unsafe(WriteErrorKind::UnsafeOpaqueAttribute,
                                "code attribute may depend on stale offsets or handler indices");
          !handled)
        return handled;
      continue;
    }

    std::pmr::vector<std::byte> relocated(&attribute_scratch);
    std::span<const std::byte> final_bytes = *bytes;
    if (spec) {
      BinaryReader reader(*bytes);
      CodeOffsetVisitor visitor;
      Relocation relocation{code, encoded, instruction_offsets, handler_offsets, {}, original_size};
      if (rewritten && anchored) {
        relocated.assign(bytes->begin(), bytes->end());
        relocation.output = relocated;
        visitor = {&relocation, [](void* state, CodeOffsetField& field) {
                     return static_cast<const Relocation*>(state)->visit(field);
                   }};
        final_bytes = relocated;
      }
      if (const auto checked = validate_opaque_attribute(reader, *spec, AttributeContext::Code, pool, ReadLimits{},
                                                         attribute_path, &context, &attribute_scratch, visitor);
          !checked) {
        if (!anchored)
          return write_error(WriteErrorKind::InvalidClassModel, attribute_path, checked.error().detail);
        if (auto handled = unsafe(WriteErrorKind::UnmappedCodeOffset, checked.error().detail); !handled)
          return handled;
        continue;
      }
    }
    if (auto written = emit(attribute.name_index(), final_bytes); !written)
      return written;
  }
  if (stack_map_name) {
    if (auto written = emit(stack_map_name, stack_map); !written)
      return written;
  }
  if (!output.patch_u2(count_offset, static_cast<std::uint16_t>(count)))
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "could not finalize Code attribute count");
  return {};
}

} // namespace cafe::detail
