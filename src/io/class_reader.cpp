/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include <cafe/io/class_reader.hpp>

#include <limits>
#include <type_traits>
#include <utility>

#include "../detail/member_signature.hpp"
#include "class_format.hpp"
#include "class_reader_impl.hpp"
#include "member_keys.hpp"

namespace cafe::detail {

ClassReader::ClassReader(std::vector<std::byte> data, const ReadOptions& options) : options(options),
                                                                                    storage(std::make_unique<ClassStorage>(std::move(data), options.upstream_resource,
                                                                                                                           options.limits.max_total_allocated_bytes)),
                                                                                    input(storage->source),
                                                                                    cp_offsets(&storage->budget) {
}

void ClassReader::diagnose(DiagnosticCode code, std::size_t offset, std::string_view path,
                           std::string_view detail, RecoveryAction action) {
  if (diagnostics.size() >= options.limits.max_diagnostics) {
    if (!suppressed_diagnostics) {
      suppressed_diagnostics = true;
      storage->budget.charge(2 * sizeof(Diagnostic) + 64);
      diagnostics.push_back(Diagnostic{Severity::Warning, DiagnosticCode::DiagnosticsSuppressed, offset, {}, "additional recovery diagnostics suppressed", RecoveryAction::None});
    }
    return;
  }
  storage->budget.charge(2 * sizeof(Diagnostic) + path.size() + detail.size());
  diagnostics.push_back(Diagnostic{Severity::Warning, code, offset, std::string(path), std::string(detail), action});
}

std::expected<ReadResult, ReadError> ClassReader::read() {
  const auto magic = input.u4();
  if (!magic)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, 0, "class", "truncated class-file magic"});
  if (*magic != CLASS_MAGIC)
    return std::unexpected(ReadError{ReadErrorKind::InvalidMagic, 0, "class", "expected class-file magic 0xcafebabe"});
  const auto minor = input.u2();
  const auto major = input.u2();
  if (!major || !minor)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, 4, "class.version", "truncated class-file version"});
  storage->version = ClassVersion{*major, *minor};
  if (*major < 45)
    return std::unexpected(ReadError{ReadErrorKind::UnsupportedDialect, 4, "class.version", "class-file dialect predates major version 45"});
  if (!storage->version.has_legal_minor()) {
    if (strict())
      return std::unexpected(ReadError{ReadErrorKind::InvalidVersion, 4, "class.version", "minor version must be 0 or 65535"});
    diagnose(DiagnosticCode::InvalidVersion, 4, "class.version", "nonstandard minor version retained", RecoveryAction::RetainedRaw);
  }

  if (auto result = read_constant_pool(); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = read_envelope(); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = check_bootstraps(); !result)
    return std::unexpected(std::move(result.error()));

  if (input.remaining() != 0) {
    if (strict())
      return std::unexpected(ReadError{ReadErrorKind::TrailingBytes, input.offset(), "class", "unexpected bytes after class attributes"});
    diagnose(DiagnosticCode::TrailingBytes, input.offset(), "class",
             "bytes after the class-file envelope will be omitted on write", RecoveryAction::Dropped);
  }
  storage->validity = has_opaque_code       ? ClassValidity::OpaqueEssentialContent
                      : diagnostics.empty() ? ClassValidity::ParsedClean
                                            : ClassValidity::ParsedRecovered;
  storage->budget.disarm();
  return ReadResult{ClassFile(std::move(storage)), std::move(diagnostics)};
}

// The interface count is checked against the remaining input before the u2 reads.
std::expected<void, ReadError> ClassReader::read_envelope() {
  const auto header_offset = input.offset();
  const auto flags = input.u2();
  const auto this_class = input.u2();
  const auto super_class = input.u2();
  if (!flags || !this_class || !super_class)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, header_offset, "class.header", "truncated class identity"});
  storage->flags = *flags;
  storage->this_class = CpIndex{*this_class};
  storage->super_class = CpIndex{*super_class};
  const auto& pool = storage->pool;
  if (const auto* issue = class_identity_issue(pool, storage->version, *flags, storage->this_class, storage->super_class))
    return std::unexpected(ReadError{ReadErrorKind::InvalidClass, header_offset, "class.header", issue});
  if (auto result = check_constant_pool(); !result)
    return result;

  const auto interface_count = input.u2();
  if (!interface_count || *interface_count > input.remaining() / 2)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, input.offset(), "interfaces", "truncated interfaces table"});
  storage->interface_entries.reserve(*interface_count);
  for (std::size_t i = 0; i < *interface_count; ++i) {
    const auto offset = input.offset();
    const CpIndex index{*input.u2()}; // NOLINT(bugprone-unchecked-optional-access)
    if (!class_name(pool, index))
      return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, offset, index_path("interfaces", i),
                                       "interface index must name a non-array class"});
    storage->interface_entries.push_back(index);
  }
  if (auto result = read_members(storage->field_entries); !result)
    return result;
  if (auto result = read_members(storage->method_entries); !result)
    return result;
  if (auto result = read_attributes(input, storage->attribute_entries, {AttributeContext::Class, ""}); !result)
    return std::unexpected(std::move(result.error()));

  if (is_module(storage->version, storage->flags)) {
    bool has_module = false;
    for (const auto& attribute : storage->attribute_entries)
      has_module |= attribute.kind() == AttributeKind::Module;
    if (!has_module || !storage->interface_entries.empty() ||
        !storage->field_entries.empty() || !storage->method_entries.empty())
      return std::unexpected(ReadError{ReadErrorKind::InvalidClass, header_offset, "class",
                                       "ACC_MODULE requires Module and empty interface, field and method tables"});
  }
  return {};
}

template<typename Member>
std::expected<void, ReadError> ClassReader::read_members(std::pmr::vector<Member>& members) {
  constexpr bool METHOD = std::is_same_v<Member, Method>;
  constexpr std::string_view LIST = METHOD ? "methods" : "fields";
  const auto count = input.u2();
  if (!count || *count > input.remaining() / 8)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, input.offset(), std::string(LIST),
                                     "truncated member table"});
  if (METHOD && *count > options.limits.max_methods)
    return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, input.offset(), "methods", "method count exceeds read limit"});
  members.reserve(*count);
  std::pmr::vector<MemberKey> keys(&storage->budget);
  if (*count > 1)
    keys.reserve(*count);
  for (std::size_t i = 0; i < *count; ++i) {
    const auto offset = input.offset();
    const auto flags = input.u2();
    const auto name = input.u2();
    const auto descriptor = input.u2();
    const auto path = index_path(LIST, i);
    if (!flags || !name || !descriptor)
      return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, offset, path, "truncated member header"});

    const auto& pool = storage->pool;
    const CpIndex name_index{*name};
    const CpIndex descriptor_index{*descriptor};
    const auto name_text = pool.get_utf8(name_index);
    const auto descriptor_text = pool.get_utf8(descriptor_index);
    if (!name_text || !descriptor_text)
      return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, offset + 2, path, "member requires valid Utf8 name and descriptor indices"});
    const auto* issue = METHOD ? method_signature_issue(storage->version, storage->flags, *flags, *name_text, *descriptor_text)
                               : field_signature_issue(*name_text, *descriptor_text);
    if (issue)
      return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, offset + 2, path, issue});
    if (*count > 1)
      keys.push_back({*name_text, *descriptor_text, i, offset});
    const bool code_required = METHOD && needs_code(*flags, *name_text);
    Member member(*flags, name_index, descriptor_index, &storage->budget);
    auto result = read_attributes(input, member.attribute_entries,
                                  {.context = METHOD ? AttributeContext::Method : AttributeContext::Field,
                                   .path = path,
                                   .descriptor = *descriptor_text,
                                   .code_required = code_required,
                                   .is_static = is_static_method(*flags, *name_text),
                                   .annotation_element = METHOD && is_annotation_element(storage->flags, *flags, *descriptor_text)});
    if (!result)
      return std::unexpected(std::move(result.error()));
    if constexpr (METHOD) {
      if (code_required && !member.code() && !member.opaque_code())
        return std::unexpected(ReadError{ReadErrorKind::MalformedEssentialCode, offset, path, "concrete method lacks Code"});
    }
    members.push_back(std::move(member));
  }
  if (const auto* duplicate = find_duplicate_member(keys))
    return std::unexpected(ReadError{ReadErrorKind::InvalidClass, duplicate->offset, index_path(LIST, duplicate->index),
                                     "duplicate member name and descriptor"});
  return {};
}

} // namespace cafe::detail

namespace cafe {

namespace {

/**
 * @brief Check both configured input size and the u4 source-slice representation limit.
 * @param size Requested byte count or checked input byte length.
 * @param options Operation-specific parsing, emission, or entry policy.
 * @return True when the input cannot be accepted safely.
 * @ingroup implementation
 */
bool exceeds_input_limit(std::size_t size, const ReadOptions& options) {
  return size > options.limits.max_input_bytes || size > std::numeric_limits<std::uint32_t>::max();
}

/**
 * @brief Describe an oversized class-file input.
 * @return A ResourceLimitExceeded read error at the class input.
 * @ingroup implementation
 */
std::unexpected<ReadError> input_limit_error() {
  return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, 0, "class",
                                   "input exceeds class-file read limits"});
}

} // namespace

std::expected<ReadResult, ReadError> read_class(std::span<const std::byte> data, const ReadOptions& options) {
  if (exceeds_input_limit(data.size(), options) || data.size() > options.limits.max_total_allocated_bytes)
    return input_limit_error();
  std::vector<std::byte> owned;
  if (!data.empty())
    owned.assign(data.begin(), data.end());
  return read_class(std::move(owned), options);
}

std::expected<ReadResult, ReadError> read_class(std::vector<std::byte>&& data, const ReadOptions& options) {
  if (exceeds_input_limit(data.size(), options) || data.capacity() > options.limits.max_total_allocated_bytes)
    return input_limit_error();
  try {
    detail::ClassReader reader(std::move(data), options);
    return reader.read();
  } catch (const detail::BudgetExceeded&) {
    return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, 0, "class",
                                     "decoded allocation budget exceeded"});
  }
}

} // namespace cafe
