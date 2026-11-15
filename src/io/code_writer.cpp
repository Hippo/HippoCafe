#include "class_writer_impl.hpp"

#include <algorithm>
#include <limits>
#include <memory_resource>

#include "../detail/descriptor.hpp"
#include "class_format.hpp"
#include "code_encoder.hpp"

namespace cafe::detail {

void ClassWriter::write_code_header(const CodeAttribute& code, std::size_t length) {
  if (short_code_header(version)) {
    output.u1(static_cast<std::uint8_t>(code.stack_limit));
    output.u1(static_cast<std::uint8_t>(code.locals_limit));
    output.u2(static_cast<std::uint16_t>(length));
  } else {
    output.u2(code.stack_limit);
    output.u2(code.locals_limit);
    output.u4(static_cast<std::uint32_t>(length));
  }
}

std::expected<void, WriteError> ClassWriter::write_code(
    const CodeAttribute& code, WriteLocation location, std::string_view path) {
  const auto& original = code.original;
  const bool rewritten = !original || code.instructions.edited() || code.supplied_metadata.has_value();
  const bool force = options.force_reencode || !options.preserve_original_encodings;
  if (!rewritten && !force) {
    if (!original->nested_recovered) {
      const auto bytes = model.source_slice(original->body);
      if (!bytes)
        return write_error(WriteErrorKind::InvalidClassModel, path, "original Code slice is out of bounds");
      output.bytes(*bytes);
      return {};
    }
    const auto bytes = model.source_slice(original->code);
    const auto table = model.source_slice(original->exceptions);
    if (!bytes || !table)
      return write_error(WriteErrorKind::InvalidClassModel, path, "original code or exception table is out of bounds");
    write_code_header(code, bytes->size());
    output.bytes(*bytes);
    output.bytes(*table);
    return write_attributes(code.attributes(), {AttributeContext::Code, path});
  }

  if (rewritten) {
    const auto& metadata = code.supplied_metadata;
    if (options.code_policy != CodeWritePolicy::CallerSuppliedMetadata ||
        !metadata || metadata->revision != code.instructions.revision() ||
        metadata->preserve_original_encodings != options.preserve_original_encodings)
      return write_error(original ? WriteErrorKind::StaleCodeMetadata : WriteErrorKind::NeedsCodeMetadata,
                         path, "supply metadata for the current instructions and output encoding");
  }
  const auto parameter_slots = *method_parameter_slots(location.descriptor) + (location.is_static ? 0u : 1u);
  if (parameter_slots > code.locals_limit)
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "max_locals cannot hold the method parameters");
  if (short_code_header(version) && (code.stack_limit > 255 || code.locals_limit > 255))
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "historical Code limits must fit u1");

  std::pmr::monotonic_buffer_resource scratch;
  const auto encoded = encode_code(code.instructions, version, pool, code.locals_limit,
                                   options.preserve_original_encodings, &scratch, path);
  if (!encoded)
    return std::unexpected(encoded.error());

  if (!rewritten) {
    const auto original_bytes = model.source_slice(original->code);
    if (!original_bytes)
      return write_error(WriteErrorKind::InvalidClassModel, path, "original code array is out of bounds");
    if (!std::ranges::equal(*original_bytes, encoded->bytes())) {
      const auto hinted = encode_code(code.instructions, version, pool, code.locals_limit, true, &scratch, path);
      if (!hinted || !std::ranges::equal(*original_bytes, hinted->bytes()) ||
          !std::ranges::equal(encoded->instruction_offsets(), hinted->instruction_offsets()))
        return write_error(WriteErrorKind::StaleCodeMetadata, path, "re-encoding would change instruction boundaries");
    }
  }

  constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max();
  if (code.handlers.size() > MAX_COUNT)
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "exception table count does not fit u2");
  const auto size = encoded->bytes().size();
  write_code_header(code, size);
  output.bytes(encoded->bytes());
  const auto count = static_cast<std::uint16_t>(code.handlers.size());
  output.u2(count);
  for (const auto& handler : code.handlers) {
    const auto start = encoded->offset_of(handler.start);
    const auto end = encoded->offset_of(handler.end);
    const auto target = encoded->offset_of(handler.handler);
    if (!start || !end || !target || *start >= *end || *end > size || *target >= size)
      return write_error(WriteErrorKind::InvalidLabel, path, "invalid exception handler range or target");
    if (handler.catch_type && !class_name(pool, handler.catch_type))
      return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "catch type must name a non-array class");
    output.u2(static_cast<std::uint16_t>(*start));
    output.u2(static_cast<std::uint16_t>(*end));
    output.u2(static_cast<std::uint16_t>(*target));
    output.u2(handler.catch_type.value);
  }
  const CodeAttributeContext context{encoded->bytes(), encoded->instruction_starts(), code.locals_limit, count};
  return write_attributes(code.attributes(), {.context = AttributeContext::Code, .path = path, .rewritten_code = rewritten, .code = &context});
}

} // namespace cafe::detail
