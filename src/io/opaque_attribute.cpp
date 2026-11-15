#include "opaque_attribute.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cafe/bytecode/opcode.hpp>
#include <cafe/constant_pool.hpp>
#include <cafe/read_options.hpp>

#include "../detail/descriptor.hpp"
#include "binary_reader.hpp"

namespace cafe::detail {
namespace {

using Validation = std::expected<void, ReadError>;

class OpaqueValidator {
public:
  OpaqueValidator(BinaryReader& body, const ConstantPool& constant_pool, const ReadLimits& read_limits,
                  std::string_view attribute_path, const CodeAttributeContext* code_context,
                  std::pmr::memory_resource* scratch_resource, CodeOffsetVisitor offset_visitor) :
      reader(body), cp(constant_pool), limits(read_limits), path(attribute_path), code(code_context),
      scratch(scratch_resource), offsets(offset_visitor) {
  }

  Validation validate(AttributeKind kind, AttributeContext context) {
    using enum AttributeKind;
    switch (kind) {
      case RuntimeVisibleAnnotations:
      case RuntimeInvisibleAnnotations:
        return annotations();
      case RuntimeVisibleParameterAnnotations:
      case RuntimeInvisibleParameterAnnotations:
        // The number of entries need not match the number in the descriptor.
        return repeat(reader.u1(), 2, [this] { return annotations(); });
      case RuntimeVisibleTypeAnnotations:
      case RuntimeInvisibleTypeAnnotations:
        return repeat(reader.u2(), 6, [this, context] { return type_annotation(context); });
      case AnnotationDefault:
        return annotation_elements(1, false);
      case LineNumberTable:
        return line_numbers();
      case LocalVariableTable:
      case LocalVariableTypeTable:
        return locals(kind);
      case StackMapTable:
        return stack_map();
      case InnerClasses:
        return inner_classes();
      case EnclosingMethod:
        return index(CpTag::Class).and_then([this] { return index(CpTag::NameAndType, true); });
      case NestHost:
      case ModuleMainClass:
        return index(CpTag::Class);
      case NestMembers:
      case PermittedSubclasses:
        return indices(CpTag::Class);
      case MethodParameters:
        return repeat(reader.u1(), 4, [this] {
          return index(CpTag::Utf8, true).and_then([this] { return skip(2); });
        });
      case Module:
        return module();
      case ModulePackages:
        return indices(CpTag::Package);
      case Record:
      case SourceDebugExtension:
      case Unknown:
        return skip(reader.remaining());
      default:
        return failure("expected a raw attribute");
    }
  }

private:
  std::unexpected<ReadError> failure_at(std::size_t offset, ReadErrorKind kind, std::string_view detail) const {
    return std::unexpected(ReadError{kind, offset, std::string(path), std::string(detail)});
  }

  std::unexpected<ReadError> failure(std::string_view detail) const {
    return failure_at(reader.offset(), ReadErrorKind::MalformedAttribute, detail);
  }

  Validation skip(std::size_t size) {
    if (!reader.take_slice(size)) {
      return failure("truncated attribute field");
    }
    return {};
  }

  std::expected<CpIndex, ReadError> read_index(CpTag tag, bool zero_allowed = false) {
    const auto offset = reader.offset();
    const auto value = reader.u2();
    if (!value) {
      return failure("truncated attribute constant pool index");
    }
    const CpIndex index{*value};
    if (!(*value == 0 && zero_allowed) && !cp.has(index, tag)) {
      return failure_at(offset, ReadErrorKind::InvalidIndex, "attribute refers to an invalid constant pool entry");
    }
    return index;
  }

  Validation index(CpTag tag, bool zero_allowed = false) {
    return read_index(tag, zero_allowed).transform([](CpIndex) {});
  }

  template<class ReadItem>
  Validation repeat(std::optional<std::size_t> count, std::size_t minimum_width, ReadItem read_item) {
    if (!count || *count > reader.remaining() / minimum_width) {
      return failure("attribute table count exceeds its remaining body");
    }
    for (std::size_t item = 0; item < *count; ++item) {
      if (auto result = read_item(); !result) {
        return result;
      }
    }
    return {};
  }

  Validation indices(CpTag tag) {
    return repeat(reader.u2(), 2, [this, tag] { return index(tag); });
  }

  [[nodiscard]] bool instruction_start(std::size_t offset) const {
    return code && offset < code->bytes.size() && offset < code->instruction_starts.size() &&
           code->instruction_starts[offset] != 0;
  }

  Validation local_range() {
    const auto offset = reader.offset();
    const auto position = reader.position();
    const auto start = reader.u2();
    const auto length = reader.u2();
    if (!start || !length) {
      return failure("truncated local variable range");
    }
    CodeOffsetField field{CodeOffsetField::Kind::Range, position, *start, *length};
    if (offsets.visit && !offsets.visit(offsets.state, field))
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "cannot relocate local variable range");
    const auto end = std::size_t(field.value) + field.length;
    if (!code || !instruction_start(field.value) || (end != code->bytes.size() && !instruction_start(end))) {
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "local variable range must use code boundaries");
    }
    return {};
  }

  Validation local_index(std::uint16_t width = 1) {
    const auto offset = reader.offset();
    const auto slot = reader.u2();
    if (!slot || !code || std::size_t(*slot) + width > code->max_locals) {
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "invalid local variable slot");
    }
    return {};
  }

  Validation line_numbers() {
    return repeat(reader.u2(), 4, [this]() -> Validation {
      const auto offset = reader.offset();
      const auto position = reader.position();
      const auto start = reader.u2();
      if (!start)
        return failure("truncated line number offset");
      CodeOffsetField field{CodeOffsetField::Kind::Line, position, *start};
      if (offsets.visit && !offsets.visit(offsets.state, field))
        return failure_at(offset, ReadErrorKind::MalformedAttribute, "cannot relocate line number offset");
      if (!code || field.value >= code->bytes.size()) {
        return failure_at(offset, ReadErrorKind::MalformedAttribute, "line number offset outside the code array");
      }
      return skip(2);
    });
  }

  Validation locals(AttributeKind kind) {
    return repeat(reader.u2(), 10, [this, kind]() -> Validation {
      if (auto range = local_range(); !range) {
        return range;
      }
      const auto name = read_index(CpTag::Utf8);
      if (!name) {
        return std::unexpected(name.error());
      }
      const auto name_text = cp.get_utf8(*name);
      if (!name_text || !valid_unqualified_name(*name_text)) {
        return failure("local variable must have a valid unqualified name");
      }
      auto descriptor = read_index(CpTag::Utf8);
      if (!descriptor) {
        return std::unexpected(std::move(descriptor).error());
      }
      const auto descriptor_text = cp.get_utf8(*descriptor);
      if (!descriptor_text) {
        return failure("local variable requires valid modified UTF-8");
      }
      if (kind == AttributeKind::LocalVariableTable) {
        auto remaining = *descriptor_text;
        const auto width = consume_field_type(remaining);
        if (!width || !remaining.empty()) {
          return failure("local variable requires a field descriptor");
        }
        return local_index(static_cast<std::uint16_t>(*width));
      }
      return local_index();
    });
  }

  Validation verification_type() {
    const auto start = reader.offset();
    const auto tag = reader.u1();
    if (tag && *tag <= 6) {
      return {};
    }
    if (tag == 7) {
      return index(CpTag::Class);
    }
    if (tag == 8) {
      const auto offset = reader.u2();
      if (offset && instruction_start(*offset) && code->bytes[*offset] == std::byte{std::to_underlying(Opcode::New)}) {
        return {};
      }
      return failure_at(start, ReadErrorKind::MalformedAttribute, "uninitialized verification type must refer to a new instruction");
    }
    return failure_at(start, ReadErrorKind::MalformedAttribute, "invalid verification type tag");
  }

  Validation stack_map() {
    std::size_t frame_offset = 0;
    bool subsequent_frame = false;
    return repeat(reader.u2(), 1, [&]() -> Validation {
      const auto start = reader.offset();
      const auto type = reader.u1();
      if (!type || (*type >= 128 && *type < 247)) {
        return failure_at(start, ReadErrorKind::MalformedAttribute, "invalid stack map frame type");
      }
      const auto delta = *type < 128 ? std::optional<std::uint16_t>(*type % 64) : reader.u2();
      if (!delta) {
        return failure("truncated stack map frame offset");
      }
      frame_offset += *delta + std::size_t(subsequent_frame);
      subsequent_frame = true;
      if (!instruction_start(frame_offset)) {
        return failure_at(start, ReadErrorKind::MalformedAttribute, "stack map frame offset is not an instruction boundary");
      }
      if ((*type >= 64 && *type < 128) || *type == 247) {
        return verification_type();
      }
      if (*type >= 252 && *type <= 254) {
        return repeat(std::size_t(*type) - 251, 1, [this] { return verification_type(); });
      }
      if (*type == 255) {
        if (auto local_types = repeat(reader.u2(), 1, [this] { return verification_type(); }); !local_types) {
          return local_types;
        }
        return repeat(reader.u2(), 1, [this] { return verification_type(); });
      }
      return {};
    });
  }

  std::expected<std::uint16_t, ReadError> annotation_header() {
    if (auto type = index(CpTag::Utf8); !type) {
      return std::unexpected(std::move(type).error());
    }
    const auto pairs = reader.u2();
    if (!pairs) {
      return failure("truncated annotation pair count");
    }
    return *pairs;
  }

  Validation annotation() {
    auto pairs = annotation_header();
    if (!pairs) {
      return std::unexpected(std::move(pairs).error());
    }
    return annotation_elements(*pairs, true);
  }

  Validation annotations() {
    return repeat(reader.u2(), 4, [this] { return annotation(); });
  }

  Validation annotation_scalar(std::uint8_t tag) {
    switch (tag) {
      case 'B':
      case 'C':
      case 'I':
      case 'S':
      case 'Z':
        return index(CpTag::Integer);
      case 'D':
        return index(CpTag::Double);
      case 'F':
        return index(CpTag::Float);
      case 'J':
        return index(CpTag::Long);
      case 's':
      case 'c':
        return index(CpTag::Utf8);
      case 'e':
        return index(CpTag::Utf8).and_then([this] { return index(CpTag::Utf8); });
      default:
        return failure_at(reader.offset() - 1, ReadErrorKind::MalformedAttribute, "invalid annotation element tag");
    }
  }

  Validation annotation_elements(std::uint16_t count, bool named) {
    // Most annotations are shallow. For deeper groups use the parser's scratch
    // resource and an explicit stack, regardless of the configured depth.
    struct Group {
      std::uint16_t remaining;
      bool named;
    };
    if (named && limits.max_annotation_depth == 0) {
      return failure_at(reader.offset(), ReadErrorKind::ResourceLimitExceeded, "annotation nesting exceeds the configured limit");
    }
    if (count == 0) {
      return {};
    }
    std::array<std::byte, 32 * sizeof(Group)> inline_stack;
    std::pmr::monotonic_buffer_resource arena(inline_stack.data(), inline_stack.size(), scratch);
    std::pmr::vector<Group> pending(&arena);

    const auto push_group = [&](std::uint16_t size, bool entries_named) -> Validation {
      if (size > reader.remaining() / (entries_named ? 5u : 3u)) {
        return failure("annotation element count exceeds its remaining body");
      }
      if (size != 0) {
        pending.push_back({size, entries_named});
      }
      return {};
    };
    if (auto result = push_group(count, named); !result) {
      return result;
    }

    // The artificial single-element root of AnnotationDefault is not nested.
    const std::size_t root_bias = named ? 0u : 1u;
    while (!pending.empty()) {
      auto& group = pending.back();
      if (group.remaining == 0) {
        pending.pop_back();
        continue;
      }
      --group.remaining;
      if (group.named) {
        if (auto name = index(CpTag::Utf8); !name) {
          return name;
        }
      }
      const auto offset = reader.offset();
      const auto tag = reader.u1();
      if (!tag) {
        return failure("truncated annotation element tag");
      }
      if (*tag == '@' || *tag == '[') {
        if (pending.size() - root_bias >= limits.max_annotation_depth) {
          return failure_at(offset, ReadErrorKind::ResourceLimitExceeded, "annotation nesting exceeds the configured limit");
        }
        if (*tag == '@') {
          auto pairs = annotation_header();
          if (!pairs) {
            return std::unexpected(std::move(pairs).error());
          }
          if (auto result = push_group(*pairs, true); !result) {
            return result;
          }
        } else {
          const auto values = reader.u2();
          if (!values) {
            return failure("truncated annotation array length");
          }
          if (auto result = push_group(*values, false); !result) {
            return result;
          }
        }
      } else if (auto result = annotation_scalar(*tag); !result) {
        return result;
      }
    }
    return {};
  }

  Validation type_annotation(AttributeContext context) {
    if (auto target = annotation_target(context); !target) {
      return target;
    }
    if (auto path_result = repeat(reader.u1(), 2, [this]() -> Validation {
          const auto step = reader.offset();
          const auto kind = reader.u1();
          const auto argument = reader.u1();
          if (!kind || !argument || *kind > 3 || (*kind != 3 && *argument != 0)) {
            return failure_at(step, ReadErrorKind::MalformedAttribute, "invalid type annotation path step");
          }
          return {};
        });
        !path_result) {
      return path_result;
    }
    return annotation();
  }

  Validation annotation_target(AttributeContext context) {
    const auto offset = reader.offset();
    const auto target = reader.u1();
    if (!target) {
      return failure("truncated type annotation target");
    }
    switch (context) {
      case AttributeContext::Class:
        if (*target == 0x00) {
          return skip(1);
        }
        if (*target == 0x10 || *target == 0x11) {
          return skip(2);
        }
        break;
      case AttributeContext::Field:
      case AttributeContext::RecordComponent:
        if (*target == 0x13) {
          return {};
        }
        break;
      case AttributeContext::Method:
        if (*target == 0x14 || *target == 0x15) {
          return {};
        }
        if (*target == 0x01 || *target == 0x16) {
          return skip(1);
        }
        if (*target == 0x12 || *target == 0x17) {
          return skip(2);
        }
        break;
      case AttributeContext::Code:
        if (*target >= 0x40 && *target <= 0x4b) {
          return code_annotation_target(*target);
        }
        break;
    }
    return failure_at(offset, ReadErrorKind::MalformedAttribute, "type annotation target is invalid in this context");
  }

  Validation code_annotation_target(std::uint8_t target) {
    if (target == 0x40 || target == 0x41) {
      return repeat(reader.u2(), 6, [this] {
        return local_range().and_then([this] { return local_index(); });
      });
    }
    const auto offset = reader.offset();
    const auto position = reader.position();
    const auto index = reader.u2();
    if (!index) {
      return failure("truncated Code type annotation target");
    }
    CodeOffsetField field{target == 0x42 ? CodeOffsetField::Kind::Handler : CodeOffsetField::Kind::InstructionAnnotation,
                          position, *index};
    if (offsets.visit && !offsets.visit(offsets.state, field))
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "cannot relocate Code type annotation target");
    if (target == 0x42) {
      if (!code || field.value >= code->exception_count) {
        return failure_at(offset, ReadErrorKind::MalformedAttribute, "type annotation refers to an invalid exception handler");
      }
      return {};
    }
    if (!instruction_start(field.value)) {
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "type annotation offset is not an instruction boundary");
    }
    return target >= 0x47 ? skip(1) : Validation{};
  }

  Validation inner_classes() {
    return repeat(reader.u2(), 8, [this]() -> Validation {
      auto inner = read_index(CpTag::Class);
      if (!inner) {
        return std::unexpected(std::move(inner).error());
      }
      auto outer = read_index(CpTag::Class, true);
      if (!outer) {
        return std::unexpected(std::move(outer).error());
      }
      if (inner->value == outer->value) {
        return failure_at(reader.offset() - 2, ReadErrorKind::MalformedAttribute, "class cannot be its own enclosing class");
      }
      return index(CpTag::Utf8, true).and_then([this] { return skip(2); });
    });
  }

  Validation module() {
    const auto header = [this](CpTag tag) {
      return index(tag).and_then([this] { return skip(2); }).and_then([this] { return index(CpTag::Utf8, true); });
    };
    if (auto result = header(CpTag::Module); !result) {
      return result;
    }
    if (auto requires_table = repeat(reader.u2(), 6, [&] { return header(CpTag::Module); }); !requires_table) {
      return requires_table;
    }
    const auto exports_or_opens = [this] {
      return repeat(reader.u2(), 6, [this] {
        return index(CpTag::Package).and_then([this] { return skip(2); }).and_then([this] { return indices(CpTag::Module); });
      });
    };
    if (auto exports_table = exports_or_opens(); !exports_table) {
      return exports_table;
    }
    if (auto opens_table = exports_or_opens(); !opens_table) {
      return opens_table;
    }
    if (auto uses_table = indices(CpTag::Class); !uses_table) {
      return uses_table;
    }
    return repeat(reader.u2(), 6, [this] {
      return index(CpTag::Class).and_then([this]() -> Validation {
        const auto providers = reader.u2();
        if (!providers || *providers == 0) {
          return failure("module provides entry must name at least one implementation");
        }
        return repeat(providers, 2, [this] { return index(CpTag::Class); });
      });
    });
  }

  BinaryReader& reader;
  const ConstantPool& cp;
  const ReadLimits& limits;
  std::string_view path;
  const CodeAttributeContext* code;
  std::pmr::memory_resource* scratch;
  CodeOffsetVisitor offsets;
};

} // namespace

Validation validate_opaque_attribute(
    BinaryReader& body, const AttributeSpec& spec, AttributeContext context,
    const ConstantPool& cp, const ReadLimits& limits, std::string_view path,
    const CodeAttributeContext* code, std::pmr::memory_resource* scratch, CodeOffsetVisitor offsets) {
  auto result = OpaqueValidator(body, cp, limits, path, code, scratch, offsets).validate(spec.kind, context);
  if (result && body.remaining() != 0) {
    return std::unexpected(ReadError{
        ReadErrorKind::MalformedAttribute, body.offset(), std::string(path), "trailing bytes in attribute body"});
  }
  return result;
}

} // namespace cafe::detail
