/**
 * @file
 * @brief Resolve and publish symbolic bytecode operands in the owning class.
 * @ingroup implementation
 */

#include <cafe/method.hpp>

#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

#include "analysis/ssa_commit.hpp"
#include "class_storage.hpp"
#include "detail/descriptor.hpp"
#include "io/class_format.hpp"

namespace cafe::detail {
namespace {

constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max(); ///< Largest class-file u2 count.

/// Result of resolving or publishing a symbolic instruction or one of its operands.
template<typename T>
using EditResult = std::expected<T, CodeEditError>;

/**
 * @brief Translate pool append errors into instruction edit errors.
 * @param error Constant-pool failure to translate.
 * @return Capacity or operand error.
 */
CodeEditError pool_error(CpError error) noexcept {
  return error == CpError::PoolFull ? CodeEditError::ConstantPoolLimit : CodeEditError::InvalidOperand;
}

/**
 * @brief Pack the significant fields of a fixed-width constant for comparison.
 * @param entry Constant whose wire fields determine its identity.
 * @return The packed wire fields, excluding the tag.
 */
std::uint64_t wire_payload(const CpEntry& entry) noexcept {
  switch (cp_format(entry.tag).form) {
    case CpForm::Bits32:
    case CpForm::Bits64:
      return entry.bits;
    case CpForm::Index:
      return entry.first.value;
    case CpForm::Pair:
      return (std::uint64_t{entry.first.value} << 16) | entry.second.value;
    case CpForm::Handle:
      return (std::uint64_t{entry.reference_kind} << 16) | entry.first.value;
    default:
      return 0;
  }
}

/**
 * @brief Combine a constant-pool tag and the hash of its payload.
 * @param tag Constant-pool tag.
 * @param payload_hash Hash of the content following the tag.
 * @return Cache key, with collisions resolved by comparing pool entries.
 */
std::size_t constant_hash(CpTag tag, std::size_t payload_hash) noexcept {
  return payload_hash * 31 + static_cast<std::size_t>(tag);
}

/**
 * @brief Hash the actual text of a Utf8 constant.
 * @param value Modified UTF-8 bytes.
 * @return Cache key for the text.
 */
std::size_t utf8_hash(std::string_view value) noexcept {
  return constant_hash(CpTag::Utf8, std::hash<std::string_view>{}(value));
}

/**
 * @brief Hash the relevant fields of a fixed-width constant.
 * @param entry Constant to hash.
 * @return Cache key for the tag and wire payload.
 */
std::size_t fixed_hash(const CpEntry& entry) noexcept {
  return constant_hash(entry.tag, std::hash<std::uint64_t>{}(wire_payload(entry)));
}

/**
 * @brief Obtain the cache key for an existing pool slot.
 * @param pool Pool supplying the slot and any UTF-8 bytes.
 * @param index Slot to index.
 * @return Its key, or nullopt for a reserved slot or malformed UTF-8.
 */
std::optional<std::size_t> pool_hash(const ConstantPool& pool, CpIndex index) noexcept {
  const auto* entry = pool.get(index);
  if (!entry)
    return std::nullopt;
  if (entry->tag != CpTag::Utf8)
    return fixed_hash(*entry);
  if (const auto text = pool.get_utf8(index))
    return utf8_hash(*text);
  return std::nullopt;
}

} // namespace

/**
 * @brief Stage one edit's pool entries and bootstrap rows and roll them back on failure.
 * @ingroup implementation
 */
class SymbolicEdit {
  friend class SsaCommit;

public:
  /**
   * @brief Record the class state before resolving any operands.
   * @param storage Class whose pool and bootstrap table the edit will update.
   */
  explicit SymbolicEdit(ClassStorage& storage) noexcept
      : storage(storage), pool(storage.pool), first_new_constant(pool.size()),
        old_utf8_count(pool.utf8_values.size()), old_string_count(pool.owned_strings.size()),
        old_attribute_count(storage.attribute_entries.size()) {
  }

  /// A transaction can have only one owner.
  SymbolicEdit(const SymbolicEdit&) = delete;
  /// Assignment would lose the transaction's original checkpoint.
  SymbolicEdit& operator=(const SymbolicEdit&) = delete;

  /// Roll back unfinished edits, including their cached indices and owned text.
  ~SymbolicEdit() {
    if (committed)
      return;
    if (storage.attribute_entries.size() != old_attribute_count) {
      storage.attribute_entries.pop_back();
    } else if (bootstraps) {
      while (bootstraps->methods.size() > old_bootstrap_count)
        bootstraps->methods.pop_back();
    }
    // Drop cache entries before releasing the pool records used to find them.
    while (storage.indexed_constants > first_new_constant) {
      const CpIndex index{static_cast<std::uint16_t>(--storage.indexed_constants)};
      if (const auto hash = pool_hash(pool, index)) {
        const auto [begin, end] = storage.constant_lookup.equal_range(*hash);
        const auto found = std::find_if(begin, end, [index](const auto& cached) { return cached.second == index; });
        if (found != end)
          storage.constant_lookup.erase(found);
      }
    }
    pool.entries.resize(first_new_constant);
    pool.utf8_values.resize(old_utf8_count);
    while (pool.owned_strings.size() > old_string_count)
      pool.owned_strings.pop_back();
  }

  /// Keep the resolved operands after the instruction edit succeeds.
  void commit() noexcept {
    committed = true;
  }

  /**
   * @brief Resolve a field instruction against this pool.
   * @param field Field opcode and symbolic member.
   * @return Compact instruction or an operand/capacity error.
   */
  EditResult<Instruction> build(const FieldAccess& field) {
    if (field.opcode < Opcode::GetStatic || field.opcode > Opcode::PutField)
      return std::unexpected(CodeEditError::WrongOperandForm);
    const auto reference = member(CpTag::Fieldref, field.owner, field.name, field.descriptor);
    if (!reference)
      return std::unexpected(reference.error());
    return Instruction::constant(field.opcode, *reference);
  }

  /**
   * @brief Resolve an ordinary invocation and derive its interface slot count if needed.
   * @param call Opcode and symbolic target.
   * @return Compact instruction or an operand/version/capacity error.
   */
  EditResult<Instruction> build(const MethodInvocation& call) {
    if (call.opcode < Opcode::InvokeVirtual || call.opcode > Opcode::InvokeInterface)
      return std::unexpected(CodeEditError::WrongOperandForm);

    const bool interface_call = call.opcode == Opcode::InvokeInterface;
    const bool interface_ref = interface_call || call.owner_is_interface;
    const bool virtual_call = call.opcode == Opcode::InvokeVirtual;
    if (virtual_call && interface_ref)
      return std::unexpected(CodeEditError::InvalidOperand);
    if (interface_ref && !interface_call && storage.version.major < 52)
      return std::unexpected(CodeEditError::UnsupportedClassVersion);
    if (call.name == "<init>" && call.opcode != Opcode::InvokeSpecial)
      return std::unexpected(CodeEditError::InvalidOperand);

    const auto slots = method_parameter_slots(call.descriptor);
    const unsigned receiver = call.opcode == Opcode::InvokeStatic ? 0 : 1;
    if (!slots || *slots + receiver > 255)
      return std::unexpected(CodeEditError::InvalidOperand);
    const auto tag = interface_ref ? CpTag::InterfaceMethodref : CpTag::Methodref;
    const auto reference = member(tag, call.owner, call.name, call.descriptor, virtual_call);
    if (!reference)
      return std::unexpected(reference.error());
    if (interface_call)
      return Instruction::invoke_interface(*reference, static_cast<std::uint8_t>(*slots + receiver));
    return Instruction::constant(call.opcode, *reference);
  }

  /**
   * @brief Resolve an invokedynamic call site.
   * @param call Name, descriptor, and bootstrap specifier.
   * @return Compact instruction or an operand/version/capacity error.
   */
  EditResult<Instruction> build(const DynamicInvocation& call) {
    return dynamic_instruction(Opcode::InvokeDynamic, CpTag::InvokeDynamic, call.name, call.descriptor, call.bootstrap);
  }

  /**
   * @brief Resolve a condy load and select its JVM stack category.
   * @param constant Name, descriptor, and bootstrap specifier.
   * @return Compact instruction or an operand/version/capacity error.
   */
  EditResult<Instruction> build(const DynamicConstant& constant) {
    const auto opcode = constant.descriptor == "J" || constant.descriptor == "D" ? Opcode::Ldc2W : Opcode::Ldc;
    return dynamic_instruction(opcode, CpTag::Dynamic, constant.name, constant.descriptor, constant.bootstrap);
  }

private:
  /** A pending bootstrap whose nested dynamic arguments are resolved in argument order. */
  struct BootstrapFrame {
    const Bootstrap* spec; ///< Borrowed specifier whose arguments are being resolved.
    const DynamicConstant* constant; ///< Borrowed argument to emit on completion, or null for the root.
    BootstrapMethod method; ///< Resolved handle and already resolved arguments.
  };

  /// Index new slots, including those appended through the low-level pool API.
  void sync_constants() {
    while (storage.indexed_constants < pool.size()) {
      const CpIndex index{static_cast<std::uint16_t>(storage.indexed_constants)};
      if (const auto hash = pool_hash(pool, index))
        storage.constant_lookup.emplace(*hash, index);
      ++storage.indexed_constants;
    }
  }

  /**
   * @brief Reuse or append owned modified UTF-8 text.
   * @param value Borrowed text to intern.
   * @return A pool index or an encoding/capacity error.
   */
  EditResult<CpIndex> utf8(std::string_view value) {
    sync_constants();
    const auto [begin, end] = storage.constant_lookup.equal_range(utf8_hash(value));
    for (auto found = begin; found != end; ++found)
      if (pool.get_utf8(found->second) == value)
        return found->second;
    return pool.append_utf8(value).transform_error(pool_error);
  }

  /**
   * @brief Reuse or append a fixed-width constant after validating its references.
   * @param entry Candidate constant referencing already resolved operands.
   * @return A pool index or an operand/version/capacity error.
   */
  EditResult<CpIndex> intern(CpEntry entry) {
    if (storage.version.major < cp_format(entry.tag).introduced_major)
      return std::unexpected(CodeEditError::UnsupportedClassVersion);
    if (cp_reference_issue(pool, entry, storage.version, false))
      return std::unexpected(CodeEditError::InvalidOperand);

    sync_constants();
    const auto payload = wire_payload(entry);
    const auto [begin, end] = storage.constant_lookup.equal_range(fixed_hash(entry));
    for (auto found = begin; found != end; ++found) {
      const auto* candidate = pool.get(found->second);
      if (candidate && candidate->tag == entry.tag && wire_payload(*candidate) == payload)
        return found->second;
    }
    return pool.append(entry).transform_error(pool_error);
  }

  /**
   * @brief Resolve a Class constant.
   * @param name Internal class name or array descriptor.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> class_constant(std::string_view name) {
    return utf8(name).and_then([&](CpIndex index) { return intern({CpTag::Class, 0, index, {}}); });
  }

  /**
   * @brief Resolve a NameAndType constant.
   * @param name Unqualified name in modified UTF-8.
   * @param descriptor Field or method descriptor in modified UTF-8.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> name_and_type(std::string_view name, std::string_view descriptor) {
    const auto name_index = utf8(name);
    if (!name_index)
      return std::unexpected(name_index.error());
    return utf8(descriptor).and_then([&](CpIndex index) {
      return intern({CpTag::NameAndType, 0, *name_index, index});
    });
  }

  /**
   * @brief Resolve the Class and NameAndType for a field or method reference.
   * @param tag Fieldref, Methodref, or InterfaceMethodref.
   * @param owner Declaring class or interface.
   * @param name Member name in modified UTF-8.
   * @param descriptor Member descriptor in modified UTF-8.
   * @param allow_array Whether virtual calls may name an array type as owner.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> member(CpTag tag, std::string_view owner, std::string_view name,
                             std::string_view descriptor, bool allow_array = false) {
    if (!allow_array && owner.starts_with('['))
      return std::unexpected(CodeEditError::InvalidOperand);
    const auto owner_index = class_constant(owner);
    if (!owner_index)
      return std::unexpected(owner_index.error());
    return name_and_type(name, descriptor).and_then([&](CpIndex index) {
      return intern({tag, 0, *owner_index, index});
    });
  }

  /**
   * @brief Resolve a symbolic handle using the reference kind and version rules.
   * @param handle Reference kind and target member.
   * @return A pool index or an operand/version/capacity error.
   */
  EditResult<CpIndex> method_handle(const MethodHandle& handle) {
    const auto kind = handle.kind;
    if (kind < MethodHandleKind::GetField || kind > MethodHandleKind::InvokeInterface)
      return std::unexpected(CodeEditError::InvalidOperand);
    const bool field = kind <= MethodHandleKind::PutStatic;
    const bool interface_ref = handle.owner_is_interface || kind == MethodHandleKind::InvokeInterface;
    if (interface_ref && (kind == MethodHandleKind::InvokeStatic || kind == MethodHandleKind::InvokeSpecial) &&
        storage.version.major < 52)
      return std::unexpected(CodeEditError::UnsupportedClassVersion);
    if (!field && kind != MethodHandleKind::InvokeStatic) {
      const auto slots = method_parameter_slots(handle.descriptor);
      if (!slots || *slots == 255)
        return std::unexpected(CodeEditError::InvalidOperand);
    }
    const auto tag = field ? CpTag::Fieldref : (interface_ref ? CpTag::InterfaceMethodref : CpTag::Methodref);
    return member(tag, handle.owner, handle.name, handle.descriptor, kind == MethodHandleKind::InvokeVirtual)
        .and_then([&](CpIndex index) { return intern({CpTag::MethodHandle, static_cast<std::uint8_t>(kind), index, {}}); });
  }

  /**
   * @brief Resolve a String bootstrap argument.
   * @param value String contents in modified UTF-8.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> argument(std::string_view value) {
    return utf8(value).and_then([&](CpIndex index) { return intern({CpTag::String, 0, index, {}}); });
  }

  /**
   * @brief Resolve a Class bootstrap argument.
   * @param value Class name or array descriptor.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> argument(ClassConstant value) {
    return class_constant(value.name);
  }

  /**
   * @brief Resolve a MethodType bootstrap argument.
   * @param value Method type descriptor.
   * @return A pool index or an operand/capacity error.
   */
  EditResult<CpIndex> argument(MethodTypeConstant value) {
    return utf8(value.descriptor).and_then([&](CpIndex index) { return intern({CpTag::MethodType, 0, index, {}}); });
  }

  /**
   * @brief Resolve a MethodHandle bootstrap argument.
   * @param value Reference kind and target member.
   * @return A pool index or an operand/version/capacity error.
   */
  EditResult<CpIndex> argument(const MethodHandle& value) {
    return method_handle(value);
  }

  /**
   * @brief Resolve a numeric bootstrap argument while preserving its bits.
   * @param value Signed Java integer or IEEE floating-point value.
   * @return A pool index or a capacity error.
   */
  template<typename T>
  EditResult<CpIndex> argument(T value) {
    static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, float> ||
                  std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>);
    constexpr bool wide = std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>;
    using Bits = std::conditional_t<wide, std::uint64_t, std::uint32_t>;
    constexpr auto tag = std::is_integral_v<T> ? (wide ? CpTag::Long : CpTag::Integer) : (wide ? CpTag::Double : CpTag::Float);
    return intern({.tag = tag, .first = {}, .second = {}, .bits = std::bit_cast<Bits>(value)});
  }

  /**
   * @brief Find or stage the single class-level bootstrap table.
   * @return The typed table or an operand/capacity error.
   */
  EditResult<BootstrapMethods*> bootstrap_table() {
    if (bootstraps)
      return bootstraps;

    BootstrapMethods* existing = nullptr;
    for (auto& attribute : storage.attribute_entries) {
      if (pool.get_utf8(attribute.name_index()) != "BootstrapMethods")
        continue;
      if (existing)
        return std::unexpected(CodeEditError::InvalidOperand);
      existing = std::get_if<BootstrapMethods>(&attribute.payload);
      if (!existing)
        return std::unexpected(CodeEditError::InvalidOperand);
    }
    if (existing) {
      old_bootstrap_count = existing->methods.size();
      bootstraps = existing;
      return bootstraps;
    }

    if (storage.attribute_entries.size() == MAX_COUNT)
      return std::unexpected(CodeEditError::BootstrapLimit);
    const auto name = utf8("BootstrapMethods");
    if (!name)
      return std::unexpected(name.error());
    BootstrapMethods table{std::pmr::vector<BootstrapMethod>(&storage.budget)};
    storage.attribute_entries.push_back(Attribute(*name, AttributeKind::BootstrapMethods, std::move(table)));
    bootstraps = std::get_if<BootstrapMethods>(&storage.attribute_entries.back().payload);
    return bootstraps;
  }

  /**
   * @brief Reuse an equal row or append an ordered bootstrap specifier.
   * @param method Resolved method handle and static argument indices.
   * @return Zero-based bootstrap ordinal or a table error.
   */
  EditResult<CpIndex> bootstrap_row(BootstrapMethod method) {
    const auto table = bootstrap_table();
    if (!table)
      return std::unexpected(table.error());
    auto& rows = (*table)->methods;
    const auto found = std::ranges::find_if(rows, [&](const BootstrapMethod& row) {
      return row.method_handle == method.method_handle && row.arguments == method.arguments;
    });
    if (found != rows.end())
      return CpIndex{static_cast<std::uint16_t>(found - rows.begin())};
    if (rows.size() == MAX_COUNT)
      return std::unexpected(CodeEditError::BootstrapLimit);
    const CpIndex index{static_cast<std::uint16_t>(rows.size())};
    rows.push_back(std::move(method));
    return index;
  }

  /**
   * @brief Resolve an argument graph into loadable constants and bootstrap rows.
   * @param root Bootstrap specifier for the requested instruction.
   * @return Its zero-based row ordinal or an operand/version/capacity error.
   */
  EditResult<CpIndex> resolve_bootstrap(const Bootstrap& root) {
    std::pmr::vector<BootstrapFrame> frames(&storage.budget);
    std::pmr::unordered_map<const DynamicConstant*, CpIndex> nested(&storage.budget);
    const auto push = [&](const Bootstrap& spec, const DynamicConstant* constant) -> EditResult<void> {
      if (spec.arguments.size() > MAX_COUNT)
        return std::unexpected(CodeEditError::BootstrapLimit);
      const auto handle = method_handle(spec.handle);
      if (!handle)
        return std::unexpected(handle.error());
      frames.push_back({&spec, constant, {*handle, std::pmr::vector<CpIndex>(&storage.budget)}});
      return {};
    };
    if (const auto started = push(root, nullptr); !started)
      return std::unexpected(started.error());

    // Resolve nested dynamic constants without recursion. A zero memoized index marks a node on
    // the active path, so back edges in a borrowed argument graph cannot hang or overflow the stack.
    for (;;) {
      auto& frame = frames.back();
      if (frame.method.arguments.size() == frame.spec->arguments.size()) {
        const auto row = bootstrap_row(std::move(frame.method));
        if (!row || !frame.constant)
          return row;
        const auto index = dynamic_entry(CpTag::Dynamic, frame.constant->name, frame.constant->descriptor, *row);
        if (!index)
          return std::unexpected(index.error());
        nested.at(frame.constant) = *index;
        frames.pop_back();
        frames.back().method.arguments.push_back(*index);
        continue;
      }

      const auto& value = frame.spec->arguments[frame.method.arguments.size()].value;
      const auto* constant = std::get_if<DynamicConstant>(&value);
      if (constant) {
        if (storage.version.major < cp_format(CpTag::Dynamic).introduced_major)
          return std::unexpected(CodeEditError::UnsupportedClassVersion);
        const auto [seen, inserted] = nested.try_emplace(constant);
        if (inserted) {
          if (const auto status = push(constant->bootstrap, constant); !status)
            return std::unexpected(status.error());
        } else {
          if (!seen->second)
            return std::unexpected(CodeEditError::InvalidOperand);
          frame.method.arguments.push_back(seen->second);
        }
        continue;
      }

      const auto index = std::visit([&]<typename T>(const T& arg) -> EditResult<CpIndex> {
        if constexpr (std::is_same_v<T, DynamicConstant>)
          return std::unexpected(CodeEditError::InvalidOperand); // Handled by the traversal above.
        else
          return argument(arg);
      },
                                    value);
      if (!index)
        return std::unexpected(index.error());
      frame.method.arguments.push_back(*index);
    }
  }

  /**
   * @brief Resolve a dynamic pool entry using an existing bootstrap ordinal.
   * @param tag Dynamic or InvokeDynamic.
   * @param name Constant or call-site name in modified UTF-8.
   * @param descriptor Field or call-site method descriptor, respectively.
   * @param bootstrap Ordinal of the bootstrap method and static argument row.
   * @return A pool index or an operand/version/capacity error.
   */
  EditResult<CpIndex> dynamic_entry(CpTag tag, std::string_view name, std::string_view descriptor, CpIndex bootstrap) {
    return name_and_type(name, descriptor).and_then([&](CpIndex index) {
      return intern({tag, 0, bootstrap, index});
    });
  }

  /**
   * @brief Build a dynamic call or load after resolving its class-level dependencies.
   * @param opcode Instruction emitted for this operand.
   * @param tag Dynamic or InvokeDynamic.
   * @param name Constant or call-site name in modified UTF-8.
   * @param descriptor Field or call-site method descriptor, respectively.
   * @param bootstrap Bootstrap method and static arguments.
   * @return A compact instruction or an operand/version/capacity error.
   */
  EditResult<Instruction> dynamic_instruction(Opcode opcode, CpTag tag, std::string_view name,
                                              std::string_view descriptor, const Bootstrap& bootstrap) {
    if (storage.version.major < cp_format(tag).introduced_major)
      return std::unexpected(CodeEditError::UnsupportedClassVersion);
    const auto row = resolve_bootstrap(bootstrap);
    if (!row)
      return std::unexpected(row.error());
    return dynamic_entry(tag, name, descriptor, *row)
        .and_then([&](CpIndex index) { return Instruction::constant(opcode, index); });
  }

  ClassStorage& storage; ///< Class that owns the table, pool, and symbolic lookup.
  ConstantPool& pool; ///< Destination pool used to resolve the edited operands.
  std::size_t first_new_constant; ///< First pool slot reserved by this edit.
  std::size_t old_utf8_count; ///< Original text record count.
  std::size_t old_string_count; ///< Original owned text buffer count.
  std::size_t old_attribute_count; ///< Original class attribute count.
  BootstrapMethods* bootstraps = nullptr; ///< Existing or staged table, discovered on first use.
  std::size_t old_bootstrap_count = 0; ///< Original row count when reusing a table.
  bool committed = false; ///< True after the method accepts the edit.
};

SsaCommit::SsaCommit(ClassFile& owner, std::size_t method_index) :
    owner(owner), method_index(method_index), staged(&owner.storage->budget),
    transaction(std::make_unique<SymbolicEdit>(*owner.storage)) {
}

SsaCommit::~SsaCommit() = default;

void SsaCommit::accept_constants() noexcept {
  transaction->commit();
}

std::expected<CpIndex, CodeEditError> SsaCommit::utf8(std::string_view value) {
  return transaction->utf8(value);
}

std::expected<InstructionBuilder, CodeEditError> SsaCommit::resolve(const MethodInstruction& value) {
  return std::visit([&]<typename T>(const T& operand) -> EditResult<InstructionBuilder> {
    if constexpr (std::is_convertible_v<T, InstructionBuilder>)
      return InstructionBuilder{operand};
    else
      return transaction->build(operand).transform([](Instruction op) { return InstructionBuilder{op}; });
  },
                    value);
}

std::expected<CpIndex, CodeEditError> SsaCommit::resolve(const BootstrapArgument& value) {
  return std::visit([&]<typename T>(const T& operand) -> EditResult<CpIndex> {
    if constexpr (std::is_same_v<T, DynamicConstant>)
      return transaction->build(operand).transform([](Instruction instruction) {
        return *instruction.constant_index();
      });
    else
      return transaction->argument(operand);
  },
                    value.value);
}

namespace {

/**
 * @brief Apply a bytecode edit, resolving a symbolic operand transactionally when needed.
 * @param storage Owning class for symbolic pool and bootstrap references.
 * @param code Decoded Code attribute, or null for a method without editable code.
 * @param value Instruction to materialize and edit.
 * @param apply Existing MethodCode edit operation.
 * @return The underlying operation's result, or a code/operand/version/capacity error.
 */
template<typename Apply>
auto edit_instruction(ClassStorage& storage, CodeAttribute* code, const MethodInstruction& value, Apply apply) {
  using Result = decltype(apply(code->code(), InstructionBuilder{}));
  if (!code)
    return Result(std::unexpected(CodeEditError::NoCode));

  return std::visit([&]<typename T>(const T& operand) -> Result {
    if constexpr (std::is_convertible_v<T, InstructionBuilder>) {
      return apply(code->code(), operand);
    } else {
      SymbolicEdit edit(storage);
      const auto instruction = edit.build(operand);
      if (!instruction)
        return std::unexpected(instruction.error());

      auto result = apply(code->code(), *instruction);
      if (result)
        edit.commit();
      return result;
    }
  },
                    value);
}

} // namespace
} // namespace cafe::detail

namespace cafe {

std::expected<InstructionId, CodeEditError> MethodEditor::append(const MethodInstruction& value) {
  return detail::edit_instruction(*storage, code(), value,
                                  [](MethodCode& body, const InstructionBuilder& builder) { return body.append(builder); });
}

std::expected<InstructionId, CodeEditError> MethodEditor::insert_before(InstructionId at, const MethodInstruction& value) {
  return detail::edit_instruction(*storage, code(), value, [at](MethodCode& body, const InstructionBuilder& builder) {
    return body.insert_before(at, builder);
  });
}

std::expected<InstructionId, CodeEditError> MethodEditor::insert_at(LabelId at, const MethodInstruction& value) {
  return detail::edit_instruction(*storage, code(), value, [at](MethodCode& body, const InstructionBuilder& builder) {
    return body.insert_at(at, builder);
  });
}

std::expected<void, CodeEditError> MethodEditor::replace(InstructionId at, const MethodInstruction& value) {
  return detail::edit_instruction(*storage, code(), value, [at](MethodCode& body, const InstructionBuilder& builder) {
    return body.replace(at, builder);
  });
}

std::expected<void, CodeEditError> MethodEditor::erase(InstructionId id) {
  auto* body = code();
  if (!body)
    return std::unexpected(CodeEditError::NoCode);
  return body->code().erase(id);
}

} // namespace cafe
