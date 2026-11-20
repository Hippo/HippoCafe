/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

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

/**
 * @brief Success or a structural ReadError while validating a bounded opaque attribute.
 */
using Validation = std::expected<void, ReadError>;

/**
 * @brief Bounded structural validator for recognized raw attributes. It retains original payload
 * bytes and can visit Code offsets for later relocation; depth and count limits bound nested
 * annotations.
 * @ingroup implementation
 */
class OpaqueValidator {
public:
  /**
   * @brief Initialize bounded structural validator for recognized raw attributes
   * @param body Length-bounded attribute body reader.
   * @param constant_pool Owning class constant pool used for reference validation.
   * @param read_limits Resource ceilings for bounded structural parsing.
   * @param attribute_path Borrowed structural diagnostic path for the attribute.
   * @param code_context Optional bounded Code bytes, instruction boundaries, local limit, and
   *   handler-count context.
   * @param scratch_resource Temporary PMR resource that must outlive scratch-backed returned views.
   * @param offset_visitor Optional borrowed callback invoked for validated code-offset fields.
   * @ingroup implementation
   */
  OpaqueValidator(BinaryReader& body, const ConstantPool& constant_pool, const ReadLimits& read_limits,
                  std::string_view attribute_path, const CodeAttributeContext* code_context,
                  std::pmr::memory_resource* scratch_resource, CodeOffsetVisitor offset_visitor) :
      reader(body), cp(constant_pool), limits(read_limits), path(attribute_path), code(code_context),
      scratch(scratch_resource), offsets(offset_visitor) {
  }

  /**
   * @brief Dispatch a recognized attribute codec and require complete bounded-body consumption.
   * @param kind Payload or error discriminator.
   * @param context Structural attribute location checked against the format registry.
   * @return Success or a malformed/trailing-body error.
   * @ingroup implementation
   */
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
  /**
   * @brief Create a raw-attribute structural error at an absolute source offset.
   * @param offset Byte offset into the supplied source/output range.
   * @param kind Payload or error discriminator.
   * @param detail Explanation copied into the structured error.
   * @return An unexpected ReadError.
   * @ingroup implementation
   */
  std::unexpected<ReadError> failure_at(std::size_t offset, ReadErrorKind kind, std::string_view detail) const {
    return std::unexpected(ReadError{kind, offset, std::string(path), std::string(detail)});
  }

  /**
   * @brief Report malformed raw-attribute content at the current reader offset.
   * @param detail Explanation copied into the structured error.
   * @return An unexpected ReadError.
   * @ingroup implementation
   */
  std::unexpected<ReadError> failure(std::string_view detail) const {
    return failure_at(reader.offset(), ReadErrorKind::MalformedAttribute, detail);
  }

  /**
   * @brief Consume a required byte count from the bounded attribute body.
   * @param size Requested byte count or checked input byte length.
   * @return Success or a truncation error.
   * @ingroup implementation
   */
  Validation skip(std::size_t size) {
    if (!reader.take_slice(size)) {
      return failure("truncated attribute field");
    }
    return {};
  }

  /**
   * @brief Read and validate a constant index against a required tag and zero policy.
   * @param tag Required constant-pool tag.
   * @param zero_allowed Whether the enclosing attribute permits a zero constant index.
   * @return The checked index, or a structural/reference failure.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate a constant reference without retaining its value.
   * @param tag Required constant-pool tag.
   * @param zero_allowed Whether the enclosing attribute permits a zero constant index.
   * @return Success or a structural/reference failure.
   * @ingroup implementation
   */
  Validation index(CpTag tag, bool zero_allowed = false) {
    return read_index(tag, zero_allowed).transform([](CpIndex) {});
  }

  /**
   * @brief Validate a bounded counted sequence before invoking its item parser.
   * @param count Number of entries or slots to reserve or encode.
   * @param minimum_width Minimum encoded bytes per item used to bound the declared count.
   * @param read_item Callback parsing one already count-bounded item.
   * @return Success or a count/truncation/item error.
   * @ingroup implementation
   */
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

  /**
   * @brief Read a u2-counted sequence of constant references with the required tag.
   * @param tag Required constant-pool tag.
   * @return Success or a count/reference error.
   * @ingroup implementation
   */
  Validation indices(CpTag tag) {
    return repeat(reader.u2(), 2, [this, tag] { return index(tag); });
  }

  /**
   * @brief Check an offset against the available Code instruction-start bitmap.
   * @param offset Byte offset into the supplied source/output range.
   * @return True for a legal instruction start, or for an unchecked context without Code metadata as
   *   defined by the caller.
   * @ingroup implementation
   */
  [[nodiscard]] bool instruction_start(std::size_t offset) const {
    return code && offset < code->bytes.size() && offset < code->instruction_starts.size() &&
           code->instruction_starts[offset] != 0;
  }

  /**
   * @brief Validate a local-variable scope and visit its offset/length fields.
   * @return Success or an invalid range/boundary/visitor failure.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate a local slot and its category width against max_locals.
   * @param width Number of local slots occupied by the referenced value.
   * @return Success or an out-of-range slot error.
   * @ingroup implementation
   */
  Validation local_index(std::uint16_t width = 1) {
    const auto offset = reader.offset();
    const auto slot = reader.u2();
    if (!slot || !code || std::size_t(*slot) + width > code->max_locals) {
      return failure_at(offset, ReadErrorKind::MalformedAttribute, "invalid local variable slot");
    }
    return {};
  }

  /**
   * @brief Validate LineNumberTable instruction offsets and line records.
   * @return Success or a malformed offset/table error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate local-variable or local-variable-type table records.
   * @param kind Payload or error discriminator.
   * @return Success or a descriptor/range/reference/table error.
   * @ingroup implementation
   */
  Validation locals(AttributeKind kind) {
    return repeat(reader.u2(), 10, [this, kind]() -> Validation {
      if (auto range = local_range(); !range) {
        return range;
      }
      auto name = read_index(CpTag::Utf8);
      if (!name) {
        return std::unexpected(std::move(name.error()));
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

  /**
   * @brief Validate one stack-map verification_type_info and any allocation-site reference.
   * @return Success or an invalid verification encoding/reference error.
   * @ingroup implementation
   */
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

  /**
   * @brief Walk StackMapTable entries while checking frame tags, offsets, and verification values.
   * @return Success or an invalid/truncated frame-table error.
   * @ingroup implementation
   */
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

  /**
   * @brief Read and validate an annotation type descriptor and pair count.
   * @return The pair count, or an invalid/truncated annotation header error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate one annotation's type and bounded element-value pairs.
   * @return Success or an annotation/reference/depth error.
   * @ingroup implementation
   */
  Validation annotation() {
    auto pairs = annotation_header();
    if (!pairs) {
      return std::unexpected(std::move(pairs).error());
    }
    return annotation_elements(*pairs, true);
  }

  /**
   * @brief Validate a counted sequence of annotations.
   * @return Success or a count/annotation error.
   * @ingroup implementation
   */
  Validation annotations() {
    return repeat(reader.u2(), 4, [this] { return annotation(); });
  }

  /**
   * @brief Validate the constant-pool form required by a scalar annotation tag.
   * @param tag Required constant-pool tag.
   * @return Success or an invalid tag/reference error.
   * @ingroup implementation
   */
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

  /**
   * @brief Walk nested annotation/array values with explicit bounded traversal state.
   * @param count Number of entries or slots to reserve or encode.
   * @param named Whether each annotation value has an element-name prefix.
   * @return Success or a malformed/reference/depth/resource error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate one type-annotation target, path, and payload.
   * @param context Structural attribute location checked against the format registry.
   * @return Success or a location/offset/path/annotation error.
   * @ingroup implementation
   */
  Validation type_annotation(AttributeContext context) {
    const auto begin = reader.position();
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
    auto checked = annotation();
    if (checked && offsets.type_annotation)
      offsets.type_annotation(offsets.state, begin, reader.position());
    return checked;
  }

  /**
   * @brief Validate a type-annotation target appropriate to its structural context.
   * @param context Structural attribute location checked against the format registry.
   * @return Success or an invalid target/context error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate and visit Code-relative type-annotation target fields.
   * @param target Method-scoped branch destination.
   * @return Success or an invalid offset/range/handler/visitor error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate the fixed-width InnerClasses records and their references.
   * @return Success or a framing/reference error.
   * @ingroup implementation
   */
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

  /**
   * @brief Validate the module attribute's counted reference tables.
   * @return Success or a table/reference error.
   * @ingroup implementation
   */
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

  BinaryReader& reader; ///< Borrowed bounded input cursor advanced by validation.
  const ConstantPool& cp; ///< Borrowed owning constant pool used for reference validation.
  const ReadLimits& limits; ///< Configured resource ceilings.
  std::string_view path; ///< Borrowed structural path used in diagnostics.
  const CodeAttributeContext* code; ///< Optional borrowed bounded Code context for nested-attribute validation.
  std::pmr::memory_resource* scratch; ///< Borrowed temporary allocator for work buffers and interned names.
  CodeOffsetVisitor offsets; ///< Callback visiting offset fields while validating nested Code attributes.
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
