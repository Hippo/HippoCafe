/**
 * @file
 * @brief Verify a detached SSA emission and publish its code, constants and metadata atomically.
 * @ingroup implementation
 */
#include "analysis/ssa_commit.hpp"

#include <algorithm>
#include <utility>

#include <cafe/analysis/max_locals.hpp>

#include "analysis/ssa_frames.hpp"
#include "bytecode/code_access.hpp"
#include "class_format.hpp"
#include "class_writer_impl.hpp"
#include "code_encoder.hpp"
#include "model/class_storage.hpp"
#include "ssa_relocation.hpp"

namespace cafe::detail {

std::size_t SsaCommit::class_allocation_bytes() const noexcept {
  return owner.storage->budget.live_bytes();
}

SsaSourceStamp SsaCommit::source_stamp(const ClassFile& owner, std::size_t index,
                                       const analysis::AnalysisContext& context) noexcept {
  if (index >= owner.methods().size())
    return {};
  SsaSourceStamp stamp{owner.declaration_revision(), context.hierarchy().snapshot_id()};
  if (const auto* source = owner.methods()[index].code()) {
    stamp.code_scope = BytecodeAccess::scope(source->code());
    stamp.code_revision = source->code().revision();
    stamp.metadata_revision = source->metadata_revision;
  }
  return stamp;
}

std::expected<std::vector<Diagnostic>, WriteError> SsaCommit::finish(
    const SsaSourceStamp& stamp, analysis::AnalysisContext& context,
    std::span<const ExceptionHandler> handlers, std::span<const analysis::StackMapFrame> seeds,
    const SsaCodeProvenance& provenance, bool drop_unsafe) try {
  const auto path = "methods[" + std::to_string(method_index) + "].Code";
  if (method_index >= owner.methods().size() || !stamp.declaration_revision ||
      stamp != source_stamp(owner, method_index, context))
    return write_error(WriteErrorKind::StaleAnalysisResult, path, "source method, metadata, or analysis context changed");
  const auto& method = owner.methods()[method_index];
  if (method.opaque_code())
    return write_error(WriteErrorKind::UnsupportedOpaqueCode, path, "cannot replace an opaque Code body");
  auto* source = owner.edit_code(method_index);
  const auto& limits = context.limits();
  if (limits.cancellation.stop_requested())
    return write_error(WriteErrorKind::Cancelled, path, "SSA lowering cancelled");
  if (handlers.size() > UINT16_MAX || handlers.size() > limits.max_handlers || seeds.size() > limits.max_frames)
    return write_error(WriteErrorKind::ResourceLimitExceeded, path, "exception table or verifier root limit exceeded");
  BudgetResource budget(limits.upstream_resource, limits.max_memory_bytes);
  std::pmr::monotonic_buffer_resource scratch(&budget);
  const auto needed = analysis::compute_max_locals(method, owner, staged, limits);
  if (!needed)
    return analysis_error(needed.error(), path, true, staged);
  std::size_t local_count = std::max<std::size_t>(*needed, provenance.debug_local_count);
  for (const auto& seed : seeds)
    local_count = std::max(local_count, seed.frame.locals.size());
  if (local_count > UINT16_MAX || local_count > limits.max_local_slots)
    return write_error(WriteErrorKind::ResourceLimitExceeded, path, "local slots exceed the lowering limit");
  const auto max_locals = static_cast<std::uint16_t>(local_count);
  auto& pool = owner.constant_pool();
  auto encoded = encode_code(staged, owner.version(), pool, max_locals, true, &scratch, path, true);
  if (!encoded)
    return std::unexpected(std::move(encoded.error()));

  // Canonicalize an expanded conditional so default re-encoding and subsequent raw edits see
  // exactly the same instruction and handler boundaries that were verified.
  auto* storage_resource = &owner.storage->budget;
  std::optional<MethodCode> expanded;
  std::pmr::vector<ExceptionHandler> expanded_handlers(&scratch);
  std::pmr::vector<std::uint32_t> expanded_offsets(&scratch);
  std::pmr::vector<analysis::StackMapFrame> expanded_seeds(&scratch);
  auto offsets = encoded->instruction_offsets().first(staged.size());
  if (encoded->has_expanded_conditionals()) {
    expanded.emplace(storage_resource);
    if (auto done = materialize_expanded_code(*expanded, staged, *encoded, owner.version(), pool, max_locals,
                                              handlers, seeds, expanded_handlers, expanded_offsets, expanded_seeds,
                                              &scratch, path);
        !done)
      return std::unexpected(std::move(done.error()));
    offsets = expanded_offsets;
    handlers = expanded_handlers;
    seeds = expanded_seeds;
  }
  auto& final_code = expanded ? *expanded : staged;
  auto verified = analysis::emission_frames(owner, method, final_code, handlers, context, seeds);
  if (!verified)
    return analysis_error(verified.error(), path, true, final_code, offsets);
  if (short_code_header(owner.version()) && (max_locals > UINT8_MAX || verified->max_stack() > UINT8_MAX))
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "historical Code limits must fit u1");

  std::optional<Attribute> stack_map;
  if (owner.version().major >= 50 && !verified->frames().empty()) {
    // The generic frame encoder stages class constants relative to the canonical pool.
    // Resolve them under the same rollback checkpoint first, so the stored attribute
    // refers exclusively to permanent class entries after publication.
    for (const auto& entry : verified->frames()) {
      if (limits.cancellation.stop_requested())
        return write_error(WriteErrorKind::Cancelled, path, "SSA frame emission cancelled");
      for (const auto* values : {&entry.frame.locals, &entry.frame.stack})
        for (const auto& type : *values)
          if (type.kind == analysis::VerificationKind::Object) {
            auto index = resolve(BootstrapArgument{ClassConstant{type.reference}});
            if (!index)
              return write_error(index.error() == CodeEditError::ConstantPoolLimit ? WriteErrorKind::ConstantPoolOverflow
                                                                                   : WriteErrorKind::InvalidVerificationFrame,
                                 path, "could not intern verifier frame class");
          }
    }
    auto name = utf8("StackMapTable");
    if (!name)
      return write_error(WriteErrorKind::ConstantPoolOverflow, path, "could not intern StackMapTable name");
    StackMapConstants constants(pool, &scratch);
    const InstructionOffsets at(final_code, offsets, &scratch);
    auto bytes = encode_stack_map(*verified, final_code, at, constants, &scratch, path);
    if (!bytes)
      return std::unexpected(std::move(bytes.error()));
    if (!constants.bytes().empty())
      return write_error(WriteErrorKind::InvalidVerificationFrame, path, "frame encoder required an unresolved class");
    stack_map.emplace(Attribute::raw(*name, std::vector<std::byte>(bytes->begin(), bytes->end())));
    stack_map->attribute_kind = AttributeKind::StackMapTable;
  }

  std::vector<Diagnostic> diagnostics;
  std::pmr::vector<Attribute> attributes(storage_resource);
  if (source) {
    const CodeAttributeContext output{encoded->bytes(), encoded->instruction_starts(), max_locals,
                                      static_cast<std::uint16_t>(handlers.size())};
    auto relocated = relocate_ssa_attributes(owner, *source, staged, *encoded, provenance, output,
                                             drop_unsafe, diagnostics, &scratch, path, limits,
                                             stack_map ? &*stack_map : nullptr);
    if (!relocated)
      return std::unexpected(std::move(relocated.error()));
    if (relocated->size() > UINT16_MAX)
      return write_error(WriteErrorKind::AttributeTooLarge, path, "too many nested Code attributes");
    attributes.reserve(relocated->size());
    for (auto& attribute : *relocated)
      attributes.push_back(std::move(attribute));
  } else if (stack_map) {
    attributes.push_back(std::move(*stack_map));
  }
  for (auto& attribute : attributes)
    attribute.bind_pool(owner.storage->pool_revision);
  std::pmr::vector<ExceptionHandler> final_handlers(handlers.begin(), handlers.end(), storage_resource);
  std::pmr::vector<CodeAttribute::OriginalBoundary> boundaries(storage_resource);
  if (std::ranges::any_of(attributes, [](const Attribute& attribute) {
        return attribute.kind() != AttributeKind::StackMapTable;
      })) {
    boundaries.reserve(final_code.size());
    std::size_t index = 0;
    for (const auto& entry : final_code.instructions())
      boundaries.push_back({static_cast<std::uint16_t>(offsets[index++]), entry.id, entry.instruction});
  }

  if (limits.cancellation.stop_requested())
    return write_error(WriteErrorKind::Cancelled, path, "SSA lowering cancelled before publication");
  if (source_stamp(owner, method_index, context) != stamp)
    return write_error(WriteErrorKind::StaleAnalysisResult, path, "source changed before SSA publication");
  if (!source) {
    const auto code_name = utf8("Code");
    if (!code_name)
      return write_error(WriteErrorKind::ConstantPoolOverflow, path, "could not intern Code name");
    auto added = owner.add_code(method_index, *code_name);
    if (!added)
      return write_error(WriteErrorKind::InvalidClassModel, path, "method cannot receive a new Code attribute");
    source = &added->get();
  }
  // Everything after attaching a new attribute consists of nonthrowing operations.
  BytecodeAccess::swap_storage(source->instructions, final_code);
  source->handlers.swap(final_handlers);
  source->nested_attributes.swap(attributes);
  source->original_boundaries.swap(boundaries);
  source->original_handlers.reset();
  source->original.reset();
  source->owned_origin_code_size = static_cast<std::uint16_t>(encoded->bytes().size());
  source->stack_limit = verified->max_stack();
  source->locals_limit = max_locals;
  source->supplied_metadata = CodeAttribute::SuppliedMetadata{source->code().revision(), true, true};
  source->metadata_revision = next_declaration_revision();
  accept_constants();
  return diagnostics;
} catch (const BudgetExceeded&) {
  return write_error(WriteErrorKind::ResourceLimitExceeded, "Code", "SSA emission exceeded its memory limit");
}

} // namespace cafe::detail
