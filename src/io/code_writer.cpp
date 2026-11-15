#include "class_writer_impl.hpp"

#include <algorithm>
#include <limits>
#include <memory_resource>

#include "../detail/descriptor.hpp"
#include "class_format.hpp"
#include "code_encoder.hpp"

namespace cafe::detail {

void ClassWriter::write_code_header(std::uint16_t max_stack, std::uint16_t max_locals, std::size_t length) {
  if (short_code_header(version)) {
    output.u1(static_cast<std::uint8_t>(max_stack));
    output.u1(static_cast<std::uint8_t>(max_locals));
    output.u2(static_cast<std::uint16_t>(length));
  } else {
    output.u2(max_stack);
    output.u2(max_locals);
    output.u4(static_cast<std::uint32_t>(length));
  }
}

std::expected<void, WriteError> ClassWriter::write_code(
    const CodeAttribute& code, WriteLocation location, std::string_view path) {
  if (options.code_analysis != CodeAnalysisMode::Preserve) {
    if (!location.method)
      return write_error(WriteErrorKind::InvalidClassModel, path, "Code requires an owning method");
    return write_analyzed_code(code, *location.method, path);
  }
  const auto& original = code.original;
  const bool rewritten = !original || code.instructions.edited() || code.supplied_metadata || code.original_handlers;
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
    write_code_header(code.stack_limit, code.locals_limit, bytes->size());
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

  const auto context = write_code_array(code, *encoded, code.stack_limit, code.locals_limit, path);
  if (!context)
    return std::unexpected(context.error());
  return write_attributes(code.attributes(), {.context = AttributeContext::Code, .path = path, .rewritten_code = rewritten, .code = &*context});
}

std::expected<WireExceptionHandler, WriteError> ClassWriter::resolve_handler(
    const ExceptionHandler& handler, const EncodedCode& encoded, std::string_view path) const {
  const auto start = encoded.offset_of(handler.start);
  const auto end = encoded.offset_of(handler.end);
  const auto target = encoded.offset_of(handler.handler);
  const auto size = encoded.bytes().size();
  if (!start || !end || !target || *start >= *end || *end > size || *target >= size)
    return write_error(WriteErrorKind::InvalidLabel, path, "invalid exception handler range or target");
  if (handler.catch_type && !class_name(pool, handler.catch_type))
    return write_error(WriteErrorKind::InvalidConstantPoolReference, path, "catch type must name a non-array class");
  return WireExceptionHandler{static_cast<std::uint16_t>(*start), static_cast<std::uint16_t>(*end),
                              static_cast<std::uint16_t>(*target), handler.catch_type};
}

std::expected<CodeAttributeContext, WriteError> ClassWriter::write_code_array(
    const CodeAttribute& code, const EncodedCode& encoded, std::uint16_t max_stack,
    std::uint16_t max_locals, std::string_view path) {
  if (short_code_header(version) && (max_stack > 255 || max_locals > 255))
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "historical Code limits must fit u1");
  constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max();
  if (code.handlers.size() > MAX_COUNT)
    return write_error(WriteErrorKind::InvalidCodeLayout, path, "exception table count does not fit u2");
  write_code_header(max_stack, max_locals, encoded.bytes().size());
  output.bytes(encoded.bytes());
  const auto count = static_cast<std::uint16_t>(code.handlers.size());
  output.u2(count);
  for (const auto& handler : code.handlers) {
    const auto wire = resolve_handler(handler, encoded, path);
    if (!wire)
      return std::unexpected(wire.error());
    output.u2(wire->start);
    output.u2(wire->end);
    output.u2(wire->handler);
    output.u2(wire->catch_type.value);
  }
  return CodeAttributeContext{encoded.bytes(), encoded.instruction_starts(), max_locals, count};
}

} // namespace cafe::detail
