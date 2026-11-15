#include "class_writer_impl.hpp"

#include <array>
#include <limits>
#include <string>

#include <cafe/read_options.hpp>

#include "binary_reader.hpp"
#include "class_format.hpp"

namespace cafe::detail {

namespace {

constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max();

bool typed_payload(AttributeKind kind) noexcept {
  switch (kind) {
    case AttributeKind::Code:
    case AttributeKind::SourceFile:
    case AttributeKind::Signature:
    case AttributeKind::ConstantValue:
    case AttributeKind::Synthetic:
    case AttributeKind::Deprecated:
    case AttributeKind::Exceptions:
    case AttributeKind::BootstrapMethods:
      return true;
    default:
      return false;
  }
}

} // namespace

std::expected<void, WriteError> ClassWriter::write_attributes(
    std::span<const Attribute> attributes, WriteLocation location) {
  if (attributes.size() > MAX_COUNT)
    return write_error(WriteErrorKind::InvalidClassModel, location.path, "attribute count does not fit u2");
  output.u2(static_cast<std::uint16_t>(attributes.size()));
  std::array<bool, static_cast<std::size_t>(AttributeKind::PermittedSubclasses) + 1> seen{};
  bool nest_attribute_seen = false;
  for (std::size_t i = 0; i < attributes.size(); ++i) {
    const auto& attribute = attributes[i];
    const auto name = pool.get_utf8(attribute.name_index());
    std::string path(location.path);
    if (!path.empty())
      path += '.';
    path += index_path("attributes", i);
    if (!name)
      return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "attribute name must refer to valid Utf8");
    const auto* spec = find_attribute(*name);
    if (spec) {
      path += '.';
      path += spec->name;
      if (!spec->applicable_to(version, location.context) ||
          (spec->kind == AttributeKind::Code && !location.code_required) ||
          !class_permits_attribute(spec->kind, version, model.access_flags(), location.annotation_element))
        return write_error(WriteErrorKind::InvalidClassModel, path, "attribute does not apply to the declared version or context");
      const auto slot = static_cast<std::size_t>(spec->kind);
      const bool nest_attribute = spec->kind == AttributeKind::NestHost || spec->kind == AttributeKind::NestMembers;
      if ((!spec->repeatable && seen[slot]) || (nest_attribute && nest_attribute_seen))
        return write_error(WriteErrorKind::InvalidClassModel, path, "duplicate or conflicting attribute");
      seen[slot] = true;
      nest_attribute_seen |= nest_attribute;
    }

    output.u2(attribute.name_index().value);
    const auto length_offset = output.size();
    output.u4(0);
    const auto start = output.size();
    const auto written = write_attribute_body(attribute, spec, location, path);
    if (!written)
      return written;
    const auto length = output.size() - start;
    if (length > std::numeric_limits<std::uint32_t>::max() ||
        !output.patch_u4(length_offset, static_cast<std::uint32_t>(length)))
      return write_error(WriteErrorKind::AttributeTooLarge, path, "attribute length does not fit u4");
  }
  return {};
}

std::expected<void, WriteError> ClassWriter::write_attribute_body(
    const Attribute& attribute, const AttributeSpec* spec, WriteLocation location, std::string_view path) {
  const auto kind = spec ? spec->kind : AttributeKind::Unknown;
  if (const auto* raw = attribute.get<RawAttribute>()) {
    if (typed_payload(kind))
      return write_error(WriteErrorKind::InvalidClassModel, path, "this attribute requires a typed payload");
    if (location.rewritten_code && (!spec || raw->source_slice()))
      return write_error(WriteErrorKind::UnsafeOpaqueAttribute, path, "original or unknown Code attribute may contain stale offsets");
    const auto bytes = raw->bytes(model);
    if (!bytes)
      return write_error(WriteErrorKind::InvalidClassModel, path, "raw attribute slice is out of bounds");
    if (bytes->size() > std::numeric_limits<std::uint32_t>::max())
      return write_error(WriteErrorKind::AttributeTooLarge, path, "raw attribute length does not fit u4");
    if (spec && !raw->source_slice()) {
      BinaryReader reader(*bytes);
      if (const auto checked = validate_opaque_attribute(reader, *spec, location.context, pool, ReadLimits{},
                                                         path, location.code);
          !checked)
        return write_error(WriteErrorKind::InvalidClassModel, path, checked.error().detail);
    }
    output.bytes(*bytes);
    return {};
  }

  if (kind != attribute.kind())
    return write_error(WriteErrorKind::InvalidClassModel, path, "attribute name and typed payload disagree");

  switch (kind) {
    case AttributeKind::SourceFile:
    case AttributeKind::Signature: {
      const auto index = kind == AttributeKind::SourceFile ? attribute.get<SourceFile>()->file_name
                                                           : attribute.get<Signature>()->signature;
      if (!pool.get_utf8(index))
        return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "attribute must refer to valid Utf8");
      output.u2(index.value);
      return {};
    }
    case AttributeKind::ConstantValue: {
      const auto index = attribute.get<ConstantValue>()->constant;
      if (!pool.has(index, constant_value_tag(location.descriptor)))
        return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "ConstantValue does not match its field descriptor");
      output.u2(index.value);
      return {};
    }
    case AttributeKind::Synthetic:
    case AttributeKind::Deprecated:
      return {};
    case AttributeKind::Exceptions: {
      const auto& types = attribute.get<Exceptions>()->types;
      if (types.size() > MAX_COUNT)
        return write_error(WriteErrorKind::AttributeTooLarge, path, "Exceptions count does not fit u2");
      output.u2(static_cast<std::uint16_t>(types.size()));
      for (const auto type : types) {
        if (!class_name(pool, type))
          return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "exception index must name a non-array class");
        output.u2(type.value);
      }
      return {};
    }
    case AttributeKind::BootstrapMethods: {
      const auto& methods = attribute.get<BootstrapMethods>()->methods;
      if (methods.size() > MAX_COUNT)
        return write_error(WriteErrorKind::AttributeTooLarge, path, "BootstrapMethods count does not fit u2");
      output.u2(static_cast<std::uint16_t>(methods.size()));
      for (const auto& method : methods) {
        if (!pool.has(method.method_handle, CpTag::MethodHandle))
          return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "bootstrap method requires a MethodHandle constant");
        if (method.arguments.size() > MAX_COUNT)
          return write_error(WriteErrorKind::AttributeTooLarge, path, "bootstrap argument count does not fit u2");
        output.u2(method.method_handle.value);
        output.u2(static_cast<std::uint16_t>(method.arguments.size()));
        for (const auto argument : method.arguments) {
          const auto* entry = pool.get(argument);
          if (!entry || !loadable_constant(entry->tag, version))
            return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "bootstrap argument requires a loadable constant");
          output.u2(argument.value);
        }
      }
      return {};
    }
    case AttributeKind::Code:
      if (const auto* code = attribute.get<CodeAttribute>())
        return write_code(*code, location, path);
      if (const auto* opaque = attribute.get<OpaqueCode>()) {
        if (options.code_analysis != CodeAnalysisMode::Preserve)
          return write_error(WriteErrorKind::UnsupportedOpaqueCode, path, "opaque Code cannot be analyzed");
        if (!options.allow_opaque_code || options.force_reencode || !options.preserve_original_encodings)
          return write_error(WriteErrorKind::UnsupportedOpaqueCode, path, "opaque Code requires explicit verbatim emission");
        const auto bytes = model.source_slice(opaque->body);
        if (!bytes)
          return write_error(WriteErrorKind::InvalidClassModel, path, "opaque Code slice is out of bounds");
        result.diagnostics.push_back({Severity::Warning, DiagnosticCode::OpaqueEssentialCode, output.size(),
                                      std::string(path), "wrote opaque Code verbatim", RecoveryAction::PreservedOpaque});
        output.bytes(*bytes);
        return {};
      }
      break;
    default:
      break;
  }
  return write_error(WriteErrorKind::InvalidClassModel, path, "attribute does not have a payload for its declared kind");
}

} // namespace cafe::detail
