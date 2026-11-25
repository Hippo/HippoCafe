/**
 * @file
 * @brief Own pending bytecode and builder-scoped label fixups until an edit commits.
 * @ingroup implementation
 */
#include <cafe/bytecode/code_builder.hpp>

#include <list>
#include <ranges>
#include "builder_access.hpp"
#include "model/symbol_storage.hpp"

namespace cafe {

/// Stable backing for a pending sequence and its local label identities.
struct CodeBuilder::Storage {
  /// Append a dense switch with an independent array of case labels.
  /// @param low First signed case key.
  /// @param default_target Default method label, or a local fixup placeholder.
  /// @param targets Labels or local fixup placeholders for consecutive cases.
  void append_table(std::int32_t low, LabelId default_target, std::vector<LabelId> targets) {
    decltype(dense_targets) pending;
    const auto& owned = pending.emplace_back(std::move(targets));
    instructions.emplace_back(TableSwitch{low, default_target, owned});
    dense_targets.splice(dense_targets.end(), pending);
  }

  /// Append a sparse switch with an independent case array.
  /// @param default_target Default method label, or a local fixup placeholder.
  /// @param pairs Case keys and method labels or local fixup placeholders.
  void append_lookup(LabelId default_target, std::vector<LookupPair> pairs) {
    decltype(sparse_targets) pending;
    const auto& owned = pending.emplace_back(std::move(pairs));
    instructions.emplace_back(LookupSwitch{default_target, owned});
    sparse_targets.splice(sparse_targets.end(), pending);
  }

  std::vector<MethodInstruction> instructions; ///< Owned operands in method order.
  detail::SymbolStorage symbols{std::pmr::get_default_resource()}; ///< Stable backing for symbolic operands.
  std::list<std::vector<LabelId>> dense_targets; ///< Stable target arrays for dense switches.
  std::list<std::vector<LookupPair>> sparse_targets; ///< Stable case arrays for sparse switches.
  std::vector<std::optional<std::size_t>> bindings; ///< Boundary index for each bound local label.
  std::vector<std::size_t> binding_order; ///< Label ordinals in emission order.
  std::vector<std::pair<std::size_t, std::size_t>> fixups; ///< Each local branch's instruction index and label ordinal.
  std::optional<BlockEditError> error; ///< First failed construction step.
};

CodeBuilder::Storage& CodeBuilder::state() {
  if (!storage)
    storage = std::make_shared<Storage>();
  return *storage;
}

BuilderLabel CodeBuilder::label() {
  auto& sequence = state();
  if (sequence.error)
    return {};
  sequence.bindings.emplace_back();
  return BuilderLabel(storage, sequence.bindings.size() - 1);
}

CodeBuilder& CodeBuilder::bind(const BuilderLabel& target) {
  auto& sequence = state();
  if (sequence.error)
    return *this;
  if (target.owner.lock() != storage || sequence.bindings[target.ordinal]) {
    sequence.error = BlockEditError{CodeEditError::InvalidLabel, std::nullopt};
    return *this;
  }
  sequence.binding_order.push_back(target.ordinal);
  sequence.bindings[target.ordinal] = sequence.instructions.size();
  return *this;
}

template<typename Targets, typename Publish>
CodeBuilder& CodeBuilder::with_local_targets(const BuilderLabel& first, const Targets& cases, Publish publish) {
  auto& sequence = state();
  if (sequence.error)
    return *this;
  const auto original_count = sequence.fixups.size();
  const auto append_target = [&](const BuilderLabel& target) {
    if (target.owner.lock() != storage) {
      sequence.error = BlockEditError{CodeEditError::InvalidLabel, sequence.instructions.size()};
      return false;
    }
    sequence.fixups.emplace_back(sequence.instructions.size(), target.ordinal);
    return true;
  };
  try {
    if (!append_target(first))
      return *this;
    for (const auto& target : cases) {
      if (!append_target(target)) {
        sequence.fixups.resize(original_count);
        return *this;
      }
    }
    publish(sequence);
  } catch (...) {
    sequence.fixups.resize(original_count);
    throw;
  }
  return *this;
}

CodeBuilder& CodeBuilder::local_branch(Opcode opcode, const BuilderLabel& target) {
  return with_local_targets(target, std::span<const BuilderLabel>{}, [opcode](Storage& sequence) {
    sequence.instructions.emplace_back(Branch{opcode, {}});
  });
}

CodeBuilder& CodeBuilder::local_table(std::int32_t low, const BuilderLabel& default_target, std::span<const BuilderLabel> targets) {
  return with_local_targets(default_target, targets, [&](Storage& sequence) {
    sequence.append_table(low, {}, std::vector<LabelId>(targets.size()));
  });
}

CodeBuilder& CodeBuilder::lookupswitch(const BuilderLabel& default_target, std::span<const BuilderLookupPair> pairs) {
  return with_local_targets(default_target, pairs | std::views::transform(&BuilderLookupPair::target), [&](Storage& sequence) {
    std::vector<LookupPair> cases;
    cases.reserve(pairs.size());
    for (const auto& pair : pairs)
      cases.push_back({pair.key, {}});
    sequence.append_lookup({}, std::move(cases));
  });
}

CodeBuilder& CodeBuilder::emit(const MethodInstruction& instruction) {
  auto& sequence = state();
  if (sequence.error)
    return *this;
  if (const auto* failed = std::get_if<std::unexpected<CodeEditError>>(&instruction)) {
    sequence.error = BlockEditError{failed->error(), sequence.instructions.size()};
    return *this;
  }

  // Build all backing locally so an allocation failure leaves this sequence reusable.
  if (const auto* table = std::get_if<TableSwitch>(&instruction)) {
    sequence.append_table(table->low, table->default_target, {table->targets.begin(), table->targets.end()});
  } else if (const auto* lookup = std::get_if<LookupSwitch>(&instruction)) {
    sequence.append_lookup(lookup->default_target, {lookup->pairs.begin(), lookup->pairs.end()});
  } else if (std::holds_alternative<Instruction>(instruction) || std::holds_alternative<Branch>(instruction)) {
    sequence.instructions.push_back(instruction);
  } else {
    detail::SymbolStorage pending(sequence.symbols.names.get_allocator().resource());
    sequence.instructions.push_back(pending.copy(instruction));
    sequence.symbols.names.splice(sequence.symbols.names.end(), pending.names);
    sequence.symbols.arguments.splice(sequence.symbols.arguments.end(), pending.arguments);
  }
  return *this;
}

#define HIPPOCAFE_INSN_SIMPLE(name, operation) \
  CodeBuilder& CodeBuilder::name() {           \
    return emit(insn::name());                 \
  }
#define HIPPOCAFE_INSN_LOCAL(name, operation)
#define HIPPOCAFE_INSN_BRANCH(name, operation)                 \
  CodeBuilder& CodeBuilder::name(LabelId target) {             \
    return emit(insn::name(target));                           \
  }                                                            \
  CodeBuilder& CodeBuilder::name(const BuilderLabel& target) { \
    return local_branch(Opcode::operation, target);            \
  }
#define HIPPOCAFE_INSN_FIELD(name, operation)                                                                    \
  CodeBuilder& CodeBuilder::name(std::string_view owner, std::string_view member, std::string_view descriptor) { \
    return emit(insn::name(owner, member, descriptor));                                                          \
  }
#define HIPPOCAFE_INSN_TYPE(name, operation)                    \
  CodeBuilder& CodeBuilder::name(std::string_view class_name) { \
    return emit(insn::name(class_name));                        \
  }
#include <cafe/bytecode/detail/insn_families.inc>
HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_INSN_SIMPLE
#undef HIPPOCAFE_INSN_LOCAL
#undef HIPPOCAFE_INSN_BRANCH
#undef HIPPOCAFE_INSN_FIELD
#undef HIPPOCAFE_INSN_TYPE

CodeBuilder& CodeBuilder::invokevirtual(std::string_view owner, std::string_view name, std::string_view descriptor) {
  return emit(insn::invokevirtual(owner, name, descriptor));
}

CodeBuilder& CodeBuilder::invokestatic(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface) {
  return emit(insn::invokestatic(owner, name, descriptor, owner_is_interface));
}

CodeBuilder& CodeBuilder::invokespecial(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface) {
  return emit(insn::invokespecial(owner, name, descriptor, owner_is_interface));
}

CodeBuilder& CodeBuilder::invokeinterface(std::string_view owner, std::string_view name, std::string_view descriptor) {
  return emit(insn::invokeinterface(owner, name, descriptor));
}

CodeBuilder& CodeBuilder::invokedynamic(std::string_view name, std::string_view descriptor, Bootstrap bootstrap) {
  return emit(insn::invokedynamic(name, descriptor, bootstrap));
}

CodeBuilder& CodeBuilder::newarray(ArrayType type) {
  return emit(insn::newarray(type));
}

CodeBuilder& CodeBuilder::lookupswitch(LabelId default_target, std::span<const LookupPair> pairs) {
  return emit(insn::lookupswitch(default_target, pairs));
}

namespace detail {

std::span<const MethodInstruction> BuilderAccess::instructions(const CodeBuilder& builder) noexcept {
  return builder.storage ? std::span(builder.storage->instructions) : std::span<const MethodInstruction>{};
}

std::span<const std::optional<std::size_t>> BuilderAccess::label_bindings(const CodeBuilder& builder) noexcept {
  return builder.storage ? std::span(builder.storage->bindings) : std::span<const std::optional<std::size_t>>{};
}

std::span<const std::size_t> BuilderAccess::binding_order(const CodeBuilder& builder) noexcept {
  return builder.storage ? std::span(builder.storage->binding_order) : std::span<const std::size_t>{};
}

std::span<const std::pair<std::size_t, std::size_t>> BuilderAccess::branch_fixups(const CodeBuilder& builder) noexcept {
  return builder.storage ? std::span(builder.storage->fixups) : std::span<const std::pair<std::size_t, std::size_t>>{};
}

std::optional<BlockEditError> BuilderAccess::error(const CodeBuilder& builder) noexcept {
  return builder.storage ? builder.storage->error : std::nullopt;
}

} // namespace detail
} // namespace cafe
