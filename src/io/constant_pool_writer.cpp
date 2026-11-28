/**
 * @file
 * @brief Reachability and index assignment for compact class-file emission.
 * @ingroup implementation
 */

#include "constant_pool_writer.hpp"

#include <limits>

#include <cafe/method.hpp>

#include "class_writer_impl.hpp"
#include "model/modified_utf8.hpp"

namespace cafe::detail {
namespace {

constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max(); ///< Largest constant pool slot count.

/// @param tag Constant tag being considered for deduplication.
/// @return Whether distinct source slots carry separate JVM linkage state.
bool has_linkage_identity(CpTag tag) noexcept {
  switch (tag) {
    case CpTag::Class:
    case CpTag::Fieldref:
    case CpTag::Methodref:
    case CpTag::InterfaceMethodref:
    case CpTag::MethodHandle:
    case CpTag::MethodType:
    case CpTag::Dynamic:
    case CpTag::InvokeDynamic:
      return true;
    default:
      return false;
  }
}

} // namespace

CompactConstantPool::CompactConstantPool(
    const ConstantPool& pool, ClassVersion version, bool module, std::span<const BootstrapMethod> bootstraps,
    std::pmr::memory_resource* resource) :
    pool(pool), version(version), module(module), bootstraps(bootstraps), source_ids(resource),
    generated_text(resource), keys(resource), constants(resource), output_order(resource),
    bootstrap_indices(resource), bootstrap_order(resource) {
  constants.push_back({{CpTag::Reserved}, {}});
}

CompactConstantPool::ConstantId CompactConstantPool::intern(const Key& key, bool from_source) {
  const auto id = constants.size();
  const bool source_class = from_source && key.tag == CpTag::Class;
  // JVM resolution results belong to symbolic references (JVMS 5.4.3).
  // A frame-generated Class can share with one source slot of the same name.
  if (key.tag == CpTag::Class || !has_linkage_identity(key.tag)) {
    const auto [existing, inserted] = keys.try_emplace(key, id);
    if (!inserted) {
      auto& prior = constants[existing->second];
      if (!source_class || !prior.has_source_class) {
        prior.has_source_class |= source_class;
        return existing->second;
      }
    }
  }
  constants.push_back({key, {}, source_class});
  return id;
}

std::expected<CompactConstantPool::ConstantId, WriteError> CompactConstantPool::resolve(CpIndex source) {
  if (const auto cached = source_ids.find(source.value); cached != source_ids.end())
    return cached->second;
  const auto* entry = pool.get(source);
  if (!entry)
    return write_error(WriteErrorKind::InvalidConstantPoolReference, index_path("constant_pool", source.value),
                       "reference must name an existing constant");
  const auto [form, introduced] = cp_format(entry->tag);
  if (form == CpForm::Invalid || version.major < introduced)
    return write_error(WriteErrorKind::UnsupportedDialect, index_path("constant_pool", source.value),
                       "constant-pool tag is not supported by the declared version");
  if (const auto* issue = cp_reference_issue(pool, *entry, version, module))
    return write_error(WriteErrorKind::InvalidConstantPoolReference, index_path("constant_pool", source.value), issue);

  Key key{entry->tag};
  if (form == CpForm::Utf8) {
    key.text = pool.utf8_bytes(source).value();
    has_invalid_utf8 |= !pool.get_utf8(source);
  } else if (form == CpForm::Bits32 || form == CpForm::Bits64) {
    key.bits = entry->bits;
  } else {
    if (dynamic_constant(entry->tag)) {
      const auto ordinal = entry->first.value;
      if (ordinal >= bootstraps.size())
        return write_error(WriteErrorKind::InvalidConstantPoolReference, index_path("constant_pool", source.value),
                           "invalid bootstrap method index");
      const auto [target, inserted] = bootstrap_indices.try_emplace(ordinal, bootstrap_order.size());
      if (inserted) {
        if (bootstrap_order.size() == MAX_COUNT)
          return write_error(WriteErrorKind::AttributeTooLarge, "BootstrapMethods", "bootstrap method count does not fit u2");
        bootstrap_order.push_back(ordinal);
      }
      key.first = target->second;
    } else {
      const auto first = resolve(entry->first);
      if (!first)
        return std::unexpected(first.error());
      key.first = *first;
    }
    if (form == CpForm::Pair) {
      const auto second = resolve(entry->second);
      if (!second)
        return std::unexpected(second.error());
      key.second = *second;
    }
    if (form == CpForm::Handle)
      key.reference_kind = entry->reference_kind;
  }
  const auto id = intern(key, true);
  source_ids.emplace(source.value, id);
  return id;
}

std::expected<CpIndex, WriteError> CompactConstantPool::assign(ConstantId id, std::string_view path) {
  auto& constant = constants[id];
  if (!constant.index) {
    const auto width = cp_format(constant.key.tag).form == CpForm::Bits64 ? 2u : 1u;
    if (next_index + width > MAX_COUNT)
      return write_error(WriteErrorKind::ConstantPoolOverflow, path, "constant pool is full");
    constant.index = CpIndex{next_index};
    next_index = static_cast<std::uint16_t>(next_index + width);
    output_order.push_back(id);
  }
  return constant.index;
}

std::expected<CpIndex, WriteError> CompactConstantPool::index(CpIndex source) {
  if (!source)
    return CpIndex{};
  const auto id = resolve(source);
  if (!id)
    return std::unexpected(id.error());
  return assign(*id, "constant_pool");
}

std::expected<void, WriteError> CompactConstantPool::reserve_narrow_constants(
    std::span<const Method> methods, bool preserve_encodings) {
  // At most 255 distinct source slots can be encoded as u1. Assign those
  // slots before assigning headers or dependencies.
  for (const auto& method : methods) {
    const auto* code = method.code();
    if (!code)
      continue;
    for (const auto& entry : code->code().instructions()) {
      const auto instruction = entry.instruction;
      if (instruction.opcode() != Opcode::Ldc || (preserve_encodings && instruction.encoding_hint() == EncodingHint::Wide))
        continue;
      const auto source = instruction.constant_index();
      if (source && source->value <= std::numeric_limits<std::uint8_t>::max()) {
        if (const auto assigned = index(*source); !assigned)
          return std::unexpected(assigned.error());
      }
    }
  }
  return {};
}

CpIndex CompactConstantPool::mapped(CpIndex source) const noexcept {
  const auto found = source_ids.find(source.value);
  return found == source_ids.end() ? CpIndex{} : constants[found->second].index;
}

std::expected<CompactConstantPool::ConstantId, WriteError> CompactConstantPool::intern_utf8(
    std::string_view value, std::string_view path) {
  Key key{.tag = CpTag::Utf8, .text = value};
  if (const auto found = keys.find(key); found != keys.end())
    return found->second;
  if (value.size() > MAX_COUNT || !valid_modified_utf8(value))
    return write_error(WriteErrorKind::UnrepresentableFrame, path, "frame type name is not a valid Utf8 constant");
  key.text = generated_text.emplace_back(value);
  return intern(key);
}

std::expected<CpIndex, WriteError> CompactConstantPool::utf8(std::string_view value, std::string_view path) {
  return intern_utf8(value, path).and_then([&](ConstantId id) { return assign(id, path); });
}

std::expected<CpIndex, WriteError> CompactConstantPool::class_reference(std::string_view name, std::string_view path) {
  const auto text = intern_utf8(name, path);
  if (!text)
    return std::unexpected(text.error());
  return assign(intern({.tag = CpTag::Class, .first = *text}), path);
}

std::expected<std::span<const std::size_t>, WriteError> CompactConstantPool::require_bootstraps(std::string_view path) {
  for (std::size_t row = 0; row < bootstrap_order.size(); ++row) {
    const auto& method = bootstraps[bootstrap_order[row]];
    if (method.arguments.size() > MAX_COUNT)
      return write_error(WriteErrorKind::AttributeTooLarge, path, "bootstrap argument count does not fit u2");
    if (!pool.has(method.method_handle, CpTag::MethodHandle))
      return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "bootstrap method requires a MethodHandle constant");
    if (auto handle = index(method.method_handle); !handle)
      return std::unexpected(handle.error());
    for (const auto argument : method.arguments) {
      const auto* entry = pool.get(argument);
      if (!entry || !loadable_constant(entry->tag, version))
        return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "bootstrap argument requires a loadable constant");
      if (auto reference = index(argument); !reference)
        return std::unexpected(reference.error());
    }
  }
  return bootstrap_order;
}

std::expected<void, WriteError> CompactConstantPool::assign_dependencies() {
  for (ConstantId id = 1; id < constants.size(); ++id) {
    if (auto assigned = assign(id, "constant_pool"); !assigned)
      return std::unexpected(assigned.error());
  }
  return {};
}

CpEntry CompactConstantPool::remapped_entry(const Key& key) const noexcept {
  const auto first = dynamic_constant(key.tag) ? CpIndex{static_cast<std::uint16_t>(key.first)} : constants[key.first].index;
  return {key.tag, key.reference_kind, first, constants[key.second].index, key.bits};
}

std::expected<void, WriteError> CompactConstantPool::build_model(ConstantPool& destination) {
  if (auto assigned = assign_dependencies(); !assigned)
    return assigned;
  for (const auto id : output_order) {
    const auto& key = constants[id].key;
    const auto appended = key.tag == CpTag::Utf8 ? destination.append_utf8_copy(key.text, valid_modified_utf8(key.text))
                                                 : destination.append(remapped_entry(key));
    if (!appended)
      return write_error(WriteErrorKind::InvalidClassModel, "constant_pool", "could not build the compact model pool");
  }
  return {};
}

std::expected<std::vector<std::byte>, WriteError> CompactConstantPool::finish(
    std::size_t origin, std::optional<Diagnostic>& invalid_utf8) {
  if (auto assigned = assign_dependencies(); !assigned)
    return std::unexpected(assigned.error());

  std::vector<std::byte> bytes;
  BinaryWriter writer(bytes);
  writer.u2(next_index);
  for (const auto id : output_order) {
    const auto& constant = constants[id];
    const auto& key = constant.key;
    if (key.text.size() > MAX_COUNT)
      return write_error(WriteErrorKind::InvalidClassModel, index_path("constant_pool", constant.index.value),
                         "Utf8 constant length does not fit u2");
    if (has_invalid_utf8 && !invalid_utf8 && key.tag == CpTag::Utf8 && !valid_modified_utf8(key.text)) {
      invalid_utf8.emplace(Severity::Warning, DiagnosticCode::InvalidModifiedUtf8, origin + writer.size(),
                           index_path("constant_pool", constant.index.value), "retained invalid modified UTF-8 bytes",
                           RecoveryAction::RetainedRaw);
    }
    write_constant(writer, remapped_entry(key), cp_format(key.tag).form, key.text);
  }
  return bytes;
}

} // namespace cafe::detail
