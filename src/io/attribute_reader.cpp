#include "class_reader_impl.hpp"

#include <array>
#include <utility>

#include "../detail/descriptor.hpp"
#include "class_format.hpp"
#include "code_decoder.hpp"

namespace cafe::detail {

namespace {

std::string attribute_path(std::string_view prefix, std::size_t index, const AttributeSpec* spec = nullptr) {
  std::string path(prefix);
  if (!path.empty())
    path += '.';
  path += index_path("attributes", index);
  if (spec) {
    path += '.';
    path += spec->name;
  }
  return path;
}

std::expected<BinaryReader, ReadError> read_attribute_body(BinaryReader& parent, std::string_view path) {
  const auto offset = parent.offset();
  const auto length = parent.u4();
  if (!length)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, offset, std::string(path), "truncated attribute length"});
  const auto body = parent.sub_reader(*length);
  if (!body)
    return std::unexpected(ReadError{ReadErrorKind::AttributeOverrun, offset, std::string(path),
                                     "attribute length exceeds its enclosing region"});
  return *body;
}

} // namespace

std::expected<bool, ReadError> ClassReader::read_attributes(
    BinaryReader& parent, std::pmr::vector<Attribute>& attributes, AttributeLocation location) {
  const auto count = parent.u2();
  if (!count || *count > parent.remaining() / 6)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, parent.offset(), std::string(location.path),
                                     "truncated attribute headers"});
  if (*count > options.limits.max_attributes - attribute_count)
    return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, parent.offset(), std::string(location.path),
                                     "attribute count exceeds read limit"});
  attribute_count += *count;
  attributes.reserve(*count);

  bool dropped = false;
  bool nest_attribute_seen = false;
  std::array<bool, static_cast<std::size_t>(AttributeKind::PermittedSubclasses) + 1> seen{};
  const auto& pool = storage->pool;
  for (std::size_t i = 0; i < *count; ++i) {
    const auto offset = parent.offset();
    const auto name_value = parent.u2();
    if (!name_value)
      return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, offset, attribute_path(location.path, i),
                                       "truncated attribute name index"});
    const CpIndex name{*name_value};
    const auto text = pool.get_utf8(name);
    const auto* spec = text ? find_attribute(*text) : nullptr;
    const auto path = attribute_path(location.path, i, spec);
    auto body = read_attribute_body(parent, path);
    if (!body)
      return std::unexpected(std::move(body.error()));

    const auto drop = [&](DiagnosticCode code, std::size_t at, std::string_view issue) {
      diagnose(code, at, path, issue, RecoveryAction::Dropped);
      dropped = true;
    };
    if (!text) {
      if (strict())
        return std::unexpected(ReadError{ReadErrorKind::InvalidIndex, offset, path, "attribute name must refer to valid Utf8"});
      drop(DiagnosticCode::InvalidAttributeName, offset, "attribute name must refer to valid Utf8");
      continue;
    }
    if (!spec) {
      attributes.push_back(Attribute(name, AttributeKind::Unknown, RawAttribute(source_slice_of(body->offset(), body->remaining()))));
      continue;
    }

    if (!spec->version_applies(storage->version)) {
      if (strict())
        return std::unexpected(ReadError{ReadErrorKind::VersionInapplicable, offset, path, "attribute is newer than the declared version"});
      drop(DiagnosticCode::VersionInapplicable, offset, "attribute is newer than the declared version");
      continue;
    }
    const auto kind = spec->kind;
    if (!spec->context_applies(location.context) || (kind == AttributeKind::Code && !location.code_required) ||
        !class_permits_attribute(kind, storage->version, storage->flags, location.annotation_element)) {
      if (strict())
        return std::unexpected(ReadError{ReadErrorKind::InvalidAttributeContext, offset, path, "attribute is not permitted here"});
      drop(DiagnosticCode::InvalidAttributeContext, offset, "attribute is not permitted here");
      continue;
    }
    const bool essential = kind == AttributeKind::Code || kind == AttributeKind::Module ||
                           (kind == AttributeKind::BootstrapMethods && has_dynamics);
    const bool nest_attribute = kind == AttributeKind::NestHost || kind == AttributeKind::NestMembers;
    const auto slot = static_cast<std::size_t>(kind);
    if ((!spec->repeatable && seen[slot]) || (nest_attribute && nest_attribute_seen)) {
      if (strict() || essential)
        return std::unexpected(ReadError{ReadErrorKind::DuplicateAttribute, offset, path, "duplicate or conflicting attribute"});
      drop(DiagnosticCode::DuplicateAttribute, offset, "duplicate or conflicting attribute");
      continue;
    }
    auto decoded = decode_attribute(*body, name, *spec, location, path);
    const bool trailing = decoded && body->remaining() != 0;
    if (!decoded || trailing) {
      auto error = trailing ? ReadError{ReadErrorKind::MalformedAttribute, body->offset(), path, "unexpected bytes after attribute payload"}
                            : std::move(decoded.error());
      if (strict() || essential || error.kind == ReadErrorKind::ResourceLimitExceeded)
        return std::unexpected(std::move(error));
      drop(DiagnosticCode::DroppedMalformedAttribute, error.offset, error.detail);
      continue;
    }
    seen[slot] = true;
    nest_attribute_seen |= nest_attribute;
    attributes.push_back(std::move(*decoded));
  }
  return dropped;
}

std::expected<Attribute, ReadError> ClassReader::decode_attribute(
    BinaryReader& body, CpIndex name, const AttributeSpec& spec, AttributeLocation location, std::string_view path) {
  const auto& pool = storage->pool;
  const auto fail = [&](std::string_view detail) {
    return std::unexpected(ReadError{ReadErrorKind::MalformedAttribute, body.offset(), std::string(path), std::string(detail)});
  };

  switch (spec.kind) {
    case AttributeKind::SourceFile:
    case AttributeKind::Signature:
    case AttributeKind::ConstantValue: {
      const auto value = body.u2();
      if (!value)
        return fail("missing attribute constant index");
      const CpIndex index{*value};
      if (spec.kind == AttributeKind::ConstantValue) {
        if (!pool.has(index, constant_value_tag(location.descriptor)))
          return fail("ConstantValue does not match its field descriptor");
        return Attribute(name, ConstantValue{index});
      }
      if (!pool.get_utf8(index))
        return fail("attribute index must refer to valid Utf8");
      return spec.kind == AttributeKind::SourceFile ? Attribute(name, SourceFile{index})
                                                    : Attribute(name, Signature{index});
    }
    case AttributeKind::Synthetic:
      return Attribute(name, Synthetic{});
    case AttributeKind::Deprecated:
      return Attribute(name, Deprecated{});
    case AttributeKind::Exceptions: {
      const auto count = body.u2();
      if (!count || *count > body.remaining() / 2)
        return fail("truncated Exceptions table");
      Exceptions list{std::pmr::vector<CpIndex>(&storage->budget)};
      list.types.reserve(*count);
      for (std::size_t i = 0; i < *count; ++i) {
        const CpIndex type{*body.u2()};
        if (!class_name(pool, type))
          return fail("exception index must name a non-array class");
        list.types.push_back(type);
      }
      return Attribute(name, AttributeKind::Exceptions, std::move(list));
    }
    case AttributeKind::BootstrapMethods: {
      const auto count = body.u2();
      if (!count || *count > body.remaining() / 4)
        return fail("truncated BootstrapMethods table");
      auto* resource = &storage->budget;
      BootstrapMethods table{std::pmr::vector<BootstrapMethod>(resource)};
      table.methods.reserve(*count);
      for (std::size_t i = 0; i < *count; ++i) {
        const auto index = body.u2();
        const auto argument_count = body.u2();
        if (!index || !argument_count || *argument_count > body.remaining() / 2)
          return fail("truncated bootstrap method");
        const CpIndex handle{*index};
        if (!pool.has(handle, CpTag::MethodHandle))
          return fail("bootstrap method requires a MethodHandle constant");
        BootstrapMethod method{handle, std::pmr::vector<CpIndex>(resource)};
        method.arguments.reserve(*argument_count);
        for (std::size_t arg = 0; arg < *argument_count; ++arg) {
          const CpIndex argument{*body.u2()};
          const auto* entry = pool.get(argument);
          if (!entry || !loadable_constant(entry->tag, storage->version))
            return fail("bootstrap argument requires a loadable constant");
          method.arguments.push_back(argument);
        }
        table.methods.push_back(std::move(method));
      }
      return Attribute(name, AttributeKind::BootstrapMethods, std::move(table));
    }
    case AttributeKind::Code:
      return read_code(body, name, path, location.descriptor, location.is_static);
    default: {
      const auto slice = source_slice_of(body.offset(), body.remaining());
      const auto checked = validate_opaque_attribute(body, spec, location.context, pool, options.limits,
                                                     path, location.code, &storage->budget, location.offsets);
      if (!checked)
        return std::unexpected(checked.error());
      return Attribute(name, spec.kind, RawAttribute(slice));
    }
  }
}

std::expected<void, ReadError> ClassReader::check_opaque_code_envelopes(BinaryReader parent, std::string_view path) {
  const auto count = parent.u2();
  if (!count || *count > parent.remaining() / 6)
    return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, parent.offset(), std::string(path),
                                     "truncated nested attribute headers in opaque Code"});
  if (*count > options.limits.max_attributes - attribute_count)
    return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, parent.offset(), std::string(path),
                                     "attribute count exceeds read limit"});
  attribute_count += *count;
  for (std::size_t i = 0; i < *count; ++i) {
    if (!parent.u2())
      return std::unexpected(ReadError{ReadErrorKind::TruncatedInput, parent.offset(), attribute_path(path, i),
                                       "truncated nested attribute name index in opaque Code"});
    const auto result = read_attribute_body(parent, attribute_path(path, i));
    if (!result)
      return std::unexpected(result.error());
  }
  if (parent.remaining() != 0)
    return std::unexpected(ReadError{ReadErrorKind::MalformedEssentialCode, parent.offset(), std::string(path),
                                     "trailing bytes after nested Code attribute table"});
  return {};
}

std::expected<Attribute, ReadError> ClassReader::read_code(
    BinaryReader& body, CpIndex name, std::string_view path, std::string_view descriptor, bool is_static) {
  const auto original = source_slice_of(body.offset(), body.remaining());
  const bool short_header = short_code_header(storage->version);
  const auto stack = short_header ? std::optional<std::uint16_t>(body.u1()) : body.u2();
  const auto locals = short_header ? std::optional<std::uint16_t>(body.u1()) : body.u2();
  const auto code_size = short_header ? std::optional<std::uint32_t>(body.u2()) : body.u4();
  const auto fail = [&](std::size_t offset, const char* detail) {
    return ReadError{ReadErrorKind::MalformedEssentialCode, offset, std::string(path), detail};
  };
  if (!stack || !locals || !code_size)
    return std::unexpected(fail(original.offset, "truncated Code header"));
  if (*code_size == 0 || *code_size > MAX_CODE_SIZE)
    return std::unexpected(fail(body.offset(), "code_length must be between 1 and 65535"));
  if (*code_size > options.limits.max_code_bytes)
    return std::unexpected(ReadError{ReadErrorKind::ResourceLimitExceeded, body.offset(), std::string(path),
                                     "code_length exceeds read limit"});

  const auto offset = body.offset();
  const auto bytes = body.take_slice(*code_size);
  if (!bytes)
    return std::unexpected(fail(offset, "code_length exceeds the Code body"));
  const auto exception_offset = body.offset();
  const auto count = body.u2();
  if (!count || *count > body.remaining() / 8)
    return std::unexpected(fail(exception_offset, "truncated exception table"));
  auto table = *body.sub_reader(static_cast<std::size_t>(*count) * 8);

  const auto recover_opaque = [&](ReadError error) -> std::expected<Attribute, ReadError> {
    if (strict() || !options.allow_opaque_code || error.kind == ReadErrorKind::ResourceLimitExceeded)
      return std::unexpected(std::move(error));
    if (const auto framed = check_opaque_code_envelopes(body, path); !framed)
      return std::unexpected(framed.error());
    (void)body.take_slice(body.remaining());
    diagnose(DiagnosticCode::OpaqueEssentialCode, error.offset, error.path, error.detail, RecoveryAction::PreservedOpaque);
    has_opaque_code = true;
    return Attribute(name, AttributeKind::Code, OpaqueCode{original});
  };

  const auto parameters = *method_parameter_slots(descriptor) + (is_static ? 0u : 1u);
  if (parameters > *locals)
    return recover_opaque(fail(original.offset, "max_locals cannot hold the method parameters"));

  auto attribute = Attribute::new_code(name, &storage->budget);
  auto& code = *attribute.mutable_code();
  const auto& pool = storage->pool;
  auto boundaries = decode_code(code.instructions, *bytes, offset, storage->version, pool, *locals,
                                options, &storage->budget, path);
  if (!boundaries)
    return recover_opaque(std::move(boundaries.error()));

  code.stack_limit = *stack;
  code.locals_limit = *locals;
  code.handlers.reserve(*count);
  for (std::size_t i = 0; i < *count; ++i) {
    const auto at = table.offset();
    const auto start = *table.u2();
    const auto end = *table.u2();
    const auto handler = *table.u2();
    const CpIndex type{*table.u2()};
    if (start >= end || !boundaries->is_instruction_start(start) || !boundaries->is_boundary(end) ||
        !boundaries->is_instruction_start(handler) || (type && !class_name(pool, type))) {
      return recover_opaque(fail(at, "invalid exception handler range, target or catch type"));
    }
    const auto start_label = boundaries->label_at(code.instructions, start);
    const auto end_label = boundaries->label_at(code.instructions, end);
    const auto handler_label = boundaries->label_at(code.instructions, handler);
    if (!start_label || !end_label || !handler_label)
      return recover_opaque(fail(at, "could not represent exception handler labels"));
    code.handlers.push_back({*start_label, *end_label, *handler_label, type});
  }
  const auto exception_slice = source_slice_of(exception_offset, body.offset() - exception_offset);
  const CodeAttributeContext context{*bytes, boundaries->instruction_starts(), *locals, *count};
  const CodeOffsetVisitor offsets{&*boundaries, [](void* state, CodeOffsetField& field) {
                                    auto& decoded = *static_cast<DecodedBoundaries*>(state);
                                    if (field.kind != CodeOffsetField::Kind::Handler)
                                      decoded.retain_metadata_boundary(field.value);
                                    if (field.kind == CodeOffsetField::Kind::Range)
                                      decoded.retain_metadata_boundary(std::size_t(field.value) + field.length);
                                    return true;
                                  }};
  const auto result = read_attributes(body, code.nested_attributes,
                                      {.context = AttributeContext::Code, .path = path, .code = &context, .offsets = offsets});
  if (!result)
    return std::unexpected(result.error());
  if (const auto count = boundaries->metadata_boundary_count(); count != 0) {
    code.original_boundaries.reserve(count);
    for (std::size_t index = 0; index < bytes->size(); ++index) {
      if (const auto ordinal = boundaries->metadata_ordinal(index)) {
        const auto id = code.instructions.at(*ordinal);
        code.original_boundaries.push_back({static_cast<std::uint16_t>(index), id, *code.instructions.get(id)});
      }
    }
  }
  code.original = CodeAttribute::OriginalCode{original, source_slice_of(offset, *code_size), exception_slice, *result};
  return attribute;
}

} // namespace cafe::detail
