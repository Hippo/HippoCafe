#include "stack_map_encoder.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include <cafe/analysis/frame_analyzer.hpp>
#include <cafe/bytecode/code.hpp>
#include <cafe/constant_pool.hpp>

#include "binary_writer.hpp"
#include "class_writer_impl.hpp"
#include "detail/descriptor.hpp"
#include "model/modified_utf8.hpp"

namespace cafe::detail {
namespace {

constexpr auto MAX_U2 = std::numeric_limits<std::uint16_t>::max();

std::expected<void, WriteError> write_type(
    BinaryWriter<std::pmr::vector<std::byte>>& writer, const analysis::VerificationType& type,
    const MethodCode& code, const InstructionOffsets& offsets, StackMapConstants& constants,
    std::string_view path) {
  using enum analysis::VerificationKind;
  switch (type.kind) {
    case Top:
      writer.u1(0);
      break;
    case Integer:
      writer.u1(1);
      break;
    case Float:
      writer.u1(2);
      break;
    case Double:
      writer.u1(3);
      break;
    case Long:
      writer.u1(4);
      break;
    case Null:
      writer.u1(5);
      break;
    case UninitializedThis:
      writer.u1(6);
      break;
    case Object: {
      const auto index = constants.class_reference(type.reference, path);
      if (!index)
        return std::unexpected(index.error());
      writer.u1(7);
      writer.u2(index->value);
      break;
    }
    case Uninitialized: {
      const auto offset = offsets.offset_of(type.allocation);
      const auto instruction = code.get(type.allocation);
      if (!offset || !instruction || instruction->opcode() != Opcode::New)
        return write_error(WriteErrorKind::UnrepresentableFrame, path, "allocation site must denote a final new instruction");
      writer.u1(8);
      writer.u2(*offset);
      break;
    }
    case WideContinuation:
      return write_error(WriteErrorKind::UnrepresentableFrame, path, "unexpected category-two continuation");
  }
  return {};
}

} // namespace

StackMapConstants::StackMapConstants(const ConstantPool& pool, std::pmr::memory_resource* resource) :
    pool(pool), names(resource), utf8_indices(&names), class_indices(&names), extra(resource), next_index(pool.size()) {
}

std::uint16_t StackMapConstants::count() const {
  return static_cast<std::uint16_t>(next_index);
}

void StackMapConstants::index_original_pool() {
  if (indexed)
    return;
  for (std::size_t position = 1; position < pool.size(); ++position) {
    const CpIndex index{static_cast<std::uint16_t>(position)};
    const auto* entry = pool.get(index);
    if (!entry)
      continue;
    if (entry->tag == CpTag::Utf8) {
      if (const auto name = pool.get_utf8(index))
        utf8_indices.try_emplace(*name, index);
    } else if (entry->tag == CpTag::Class) {
      if (const auto name = pool.get_utf8(entry->first))
        class_indices.try_emplace(*name, index);
    }
  }
  indexed = true;
}

std::expected<CpIndex, WriteError> StackMapConstants::utf8(std::string_view text, std::string_view path) {
  index_original_pool();
  if (const auto existing = utf8_indices.find(text); existing != utf8_indices.end())
    return existing->second;
  if (next_index >= MAX_U2)
    return write_error(WriteErrorKind::ConstantPoolOverflow, path, "constant pool is full");
  if (text.size() > MAX_U2 || !valid_modified_utf8(text))
    return write_error(WriteErrorKind::UnrepresentableFrame, path, "frame type name is not a valid Utf8 constant");
  BinaryWriter writer(extra);
  writer.u1(static_cast<std::uint8_t>(CpTag::Utf8));
  writer.u2(static_cast<std::uint16_t>(text.size()));
  writer.bytes(std::as_bytes(std::span(text)));
  auto* owned = static_cast<char*>(names.allocate(text.size(), alignof(char)));
  std::memcpy(owned, text.data(), text.size());
  const CpIndex index{static_cast<std::uint16_t>(next_index++)};
  utf8_indices.emplace(std::string_view(owned, text.size()), index);
  return index;
}

std::expected<CpIndex, WriteError> StackMapConstants::class_reference(std::string_view name, std::string_view path) {
  index_original_pool();
  if (const auto existing = class_indices.find(name); existing != class_indices.end())
    return existing->second;
  if (!(name.starts_with('[') ? valid_field_descriptor(name) : valid_class_name(name)))
    return write_error(WriteErrorKind::UnrepresentableFrame, path, "invalid reference in a stack-map frame");
  const auto text = utf8(name, path);
  if (!text)
    return std::unexpected(text.error());
  if (next_index >= MAX_U2)
    return write_error(WriteErrorKind::ConstantPoolOverflow, path, "constant pool is full");
  BinaryWriter writer(extra);
  writer.u1(static_cast<std::uint8_t>(CpTag::Class));
  writer.u2(text->value);
  const CpIndex index{static_cast<std::uint16_t>(next_index++)};
  class_indices.emplace(utf8_indices.find(name)->first, index);
  return index;
}

InstructionOffsets::InstructionOffsets(
    const MethodCode& code, std::span<const std::uint32_t> offsets, std::pmr::memory_resource* scratch) : entries(scratch) {
  if (offsets.size() < code.size())
    return;
  entries.reserve(code.size());
  for (std::size_t index = 0; index < code.size(); ++index)
    entries.push_back({code.at(index), static_cast<std::uint16_t>(offsets[index])});
  const auto by_slot = [](const Entry& entry) { return entry.instruction.index; };
  if (!std::ranges::is_sorted(entries, {}, by_slot))
    std::ranges::sort(entries, {}, by_slot);
}

std::optional<std::uint16_t> InstructionOffsets::offset_of(InstructionId id) const {
  const auto match = std::ranges::lower_bound(entries, id.index, {}, [](const Entry& entry) {
    return entry.instruction.index;
  });
  if (match == entries.end() || match->instruction != id)
    return std::nullopt;
  return match->offset;
}

std::expected<std::pmr::vector<std::byte>, WriteError> encode_stack_map(
    const analysis::FrameAnalysis& analysis, const MethodCode& code, const InstructionOffsets& offsets,
    StackMapConstants& constants, std::pmr::memory_resource* scratch, std::string_view path) {
  using enum analysis::VerificationKind;
  std::pmr::vector<std::byte> bytes(scratch);
  BinaryWriter writer(bytes);
  const auto frames = analysis.frames();
  if (frames.empty())
    return bytes;
  if (frames.size() > MAX_U2)
    return write_error(WriteErrorKind::UnrepresentableFrame, path, "frame count exceeds the u2 limit");
  writer.u2(static_cast<std::uint16_t>(frames.size()));
  int previous = -1;
  for (const auto& [instruction, frame] : frames) {
    const auto offset = offsets.offset_of(instruction);
    if (!offset || *offset <= previous)
      return write_error(WriteErrorKind::StackMapOffsetOverflow, path, "stack-map offsets must be valid and strictly increasing");
    auto locals = std::span(frame.locals);
    while (!locals.empty() && locals.back().kind == Top)
      locals = locals.first(locals.size() - 1);
    if (locals.size() > analysis.max_locals() || frame.stack.size() > analysis.max_stack())
      return write_error(WriteErrorKind::UnrepresentableFrame, path, "stack-map frame exceeds the computed maxima");

    writer.u1(255);
    writer.u2(static_cast<std::uint16_t>(*offset - previous - 1));
    previous = *offset;
    const auto continuations = std::ranges::count(locals, WideContinuation, &analysis::VerificationType::kind);
    writer.u2(static_cast<std::uint16_t>(locals.size() - static_cast<std::size_t>(continuations)));
    for (std::size_t slot = 0; slot < locals.size(); ++slot) {
      const auto& type = locals[slot];
      if (const auto written = write_type(writer, type, code, offsets, constants, path); !written)
        return std::unexpected(written.error());
      if (type.width() == 2) {
        if (++slot >= locals.size() || locals[slot].kind != WideContinuation)
          return write_error(WriteErrorKind::UnrepresentableFrame, path, "wide local lacks its second slot");
      }
    }
    writer.u2(static_cast<std::uint16_t>(frame.stack.size()));
    unsigned slots = 0;
    for (const auto& type : frame.stack) {
      if (type.kind == Top)
        return write_error(WriteErrorKind::UnrepresentableFrame, path, "operand stack contains an unusable type");
      slots += type.width();
      if (const auto written = write_type(writer, type, code, offsets, constants, path); !written)
        return std::unexpected(written.error());
    }
    if (slots != frame.stack_slots || slots > analysis.max_stack())
      return write_error(WriteErrorKind::UnrepresentableFrame, path, "operand stack exceeds the computed maximum");
  }
  return bytes;
}

} // namespace cafe::detail
