/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include "class_reader_impl.hpp"

#include "../model/modified_utf8.hpp"
#include "class_format.hpp"

namespace cafe::detail {

std::expected<void, ReadError> ClassReader::read_constant_pool() {
  const auto count_offset = input.offset();
  const auto count = input.u2();
  if (!count)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, count_offset, "constant_pool", "missing constant_pool_count"});
  if (*count == 0)
    return std::unexpected(ReadError{ReadErrorKind::InvalidConstantPool, count_offset, "constant_pool", "zero constant_pool_count"});
  if (static_cast<std::size_t>(*count - 1) > input.remaining() / 3)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, input.offset(), "constant_pool", "constant pool count exceeds available input"});

  auto& pool = storage->pool;
  pool.entries.reserve(*count);
  cp_offsets.resize(*count);
  for (std::size_t i = 1; i < *count; ++i) {
    const auto offset = input.offset();
    const auto fail = [i, offset](ReadErrorKind kind, std::string_view detail) {
      return std::unexpected(ReadError{kind, offset, index_path("constant_pool", i), std::string(detail)});
    };
    const auto wire_tag = input.u1();
    if (!wire_tag)
      return fail(ReadErrorKind::TruncatedInput, "missing constant tag");

    CpEntry entry;
    entry.tag = static_cast<CpTag>(*wire_tag);
    const auto [form, introduced] = cp_format(entry.tag);
    if (form == CpForm::Invalid)
      return fail(ReadErrorKind::UnknownConstantTag, "unrecognized constant-pool tag " + std::to_string(*wire_tag));
    if (storage->version.major < introduced) {
      if (strict())
        return fail(ReadErrorKind::VersionInapplicable, "constant-pool tag predates its introduction");
      diagnose(DiagnosticCode::VersionInapplicable, offset, index_path("constant_pool", i),
               "constant-pool tag is newer than the declared version", RecoveryAction::RetainedRaw);
    }

    cp_offsets[i] = offset;
    has_dynamics |= dynamic_constant(entry.tag);
    switch (form) {
      case CpForm::Utf8: {
        const auto size = input.u2();
        const auto bytes = size ? input.take_slice(*size) : std::nullopt;
        if (!bytes)
          return fail(ReadErrorKind::TruncatedInput, "truncated Utf8 constant");
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        const bool valid = valid_modified_utf8(text);
        if (!valid) {
          if (strict())
            return fail(ReadErrorKind::InvalidModifiedUtf8, "invalid modified UTF-8");
          diagnose(DiagnosticCode::InvalidModifiedUtf8, offset, index_path("constant_pool", i),
                   "invalid modified UTF-8 retained as bytes", RecoveryAction::RetainedRaw);
        }
        (void)pool.append_utf8_view(text, valid);
        continue;
      }
      case CpForm::Bits32: {
        const auto bits = input.u4();
        if (!bits)
          return fail(ReadErrorKind::TruncatedInput, "truncated four-byte constant");
        entry.bits = *bits;
        break;
      }
      case CpForm::Bits64: {
        if (i + 1 >= *count)
          return fail(ReadErrorKind::InvalidConstantPool, "Long or Double is missing its second slot");
        const auto bits = input.u8();
        if (!bits)
          return fail(ReadErrorKind::TruncatedInput, "truncated eight-byte constant");
        entry.bits = *bits;
        ++i;
        break;
      }
      case CpForm::Index: {
        const auto index = input.u2();
        if (!index)
          return fail(ReadErrorKind::TruncatedInput, "truncated constant index");
        entry.first = CpIndex{*index};
        break;
      }
      case CpForm::Pair: {
        const auto first = input.u2();
        const auto second = input.u2();
        if (!first || !second)
          return fail(ReadErrorKind::TruncatedInput, "truncated pair of constant indices");
        entry.first = CpIndex{*first};
        entry.second = CpIndex{*second};
        break;
      }
      case CpForm::Handle: {
        const auto kind = input.u1();
        const auto reference = input.u2();
        if (!kind || !reference)
          return fail(ReadErrorKind::TruncatedInput, "truncated MethodHandle");
        entry.reference_kind = *kind;
        entry.first = CpIndex{*reference};
        break;
      }
      case CpForm::Invalid:
        return fail(ReadErrorKind::UnknownConstantTag, "unrecognized constant-pool tag");
    }
    if (!pool.append(entry))
      return fail(ReadErrorKind::InvalidConstantPool, "constant could not be represented");
  }
  return {};
}

std::expected<void, ReadError> ClassReader::check_constant_pool() const {
  const auto& pool = storage->pool;
  for (std::size_t i = 1; i < pool.size(); ++i) {
    const auto* entry = pool.get(CpIndex{static_cast<std::uint16_t>(i)});
    if (!entry)
      continue;
    if (const auto* issue = cp_reference_issue(pool, *entry, storage->version, is_module(storage->version, storage->flags)))
      return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, cp_offsets[i], index_path("constant_pool", i), issue});
  }
  return {};
}

std::expected<void, ReadError> ClassReader::check_bootstraps() const {
  if (!has_dynamics)
    return {};
  std::size_t count = 0;
  for (const auto& attribute : storage->attribute_entries)
    if (const auto* bootstraps = attribute.get<BootstrapMethods>())
      count = bootstraps->methods.size();
  const auto& pool = storage->pool;
  for (std::size_t i = 1; i < pool.size(); ++i) {
    const auto* entry = pool.get(CpIndex{static_cast<std::uint16_t>(i)});
    if (entry && dynamic_constant(entry->tag) && entry->first.value >= count)
      return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, cp_offsets[i], index_path("constant_pool", i),
                                       "dynamic constant does not reference a valid bootstrap method"});
  }
  return {};
}

} // namespace cafe::detail
