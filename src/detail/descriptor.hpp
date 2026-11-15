/**
 * @file
 * @brief Shared descriptor and JVM member validation helpers.
 * @ingroup implementation
 */

#pragma once

#include <optional>
#include <string_view>

namespace cafe::detail {

/**
 * @brief Validate a nonempty JVM unqualified member name.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return True if forbidden separators and descriptor punctuation are absent.
 * @ingroup implementation
 */
inline bool valid_unqualified_name(std::string_view name) noexcept {
  return !name.empty() && name.find_first_of(".;[/") == std::string_view::npos;
}

/**
 * @brief Validate internal JVM class-name spelling and modified UTF-8.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return True for a nonempty slash-separated non-array class name.
 * @ingroup implementation
 */
inline bool valid_class_name(std::string_view name) noexcept {
  while (true) {
    const auto slash = name.find('/');
    if (!valid_unqualified_name(name.substr(0, slash)))
      return false;
    if (slash == std::string_view::npos)
      return true;
    name.remove_prefix(slash + 1);
  }
}

/**
 * @brief Parsed JVM descriptor categories, including void only where explicitly allowed.
 * @ingroup implementation
 */
enum class DescriptorKind {
  Void, ///< Void method return type; occupies no slots.
  Integer, ///< JVM int verification value, also used for byte/char/short/boolean computation.
  Float, ///< JVM float value.
  Long, ///< JVM long value occupying two slots.
  Double, ///< JVM double value occupying two slots.
  Reference, ///< Object or array reference descriptor.
};

/**
 * @brief Parsed field/return type with borrowed class name or array descriptor and JVM slot width.
 * @ingroup implementation
 */
struct DescriptorType {
  DescriptorKind kind; ///< Discriminator selecting the interpretation of this record.
  /**
   * @brief Borrowed internal class name or array descriptor; meaningful for reference and
   * uninitialized values.
   *
   * The internal name for classes and the full descriptor for arrays.
   */
  std::string_view reference = {};

  /**
   * @brief Count JVM slots occupied by the descriptor type.
   * @return Zero for void, two for long/double, and one for all other types.
   */
  [[nodiscard]] unsigned width() const noexcept {
    switch (kind) {
      case DescriptorKind::Void:
        return 0;
      case DescriptorKind::Long:
      case DescriptorKind::Double:
        return 2;
      default:
        return 1;
    }
  }
};

/**
 * @brief Consume one field type from a descriptor view, optionally accepting void.
 * @param text Borrowed UTF-8 text to compare, validate, or encode.
 * @return Its kind, borrowed reference text, and slot width, or nullopt on malformed input.
 * @ingroup implementation
 */
inline std::optional<DescriptorType> parse_field_type(std::string_view& text) noexcept {
  const auto rank = text.find_first_not_of('[');
  if (rank == std::string_view::npos || rank > 255)
    return std::nullopt;
  const auto tag = text[rank];
  auto end = rank + 1;
  DescriptorType type{DescriptorKind::Integer};
  if (tag == 'L') {
    const auto semicolon = text.find(';', end);
    if (semicolon == std::string_view::npos || !valid_class_name(text.substr(end, semicolon - end)))
      return std::nullopt;
    type = {DescriptorKind::Reference, text.substr(end, semicolon - end)};
    end = semicolon + 1;
  } else {
    switch (tag) {
      case 'B':
      case 'C':
      case 'I':
      case 'S':
      case 'Z':
        break;
      case 'F':
        type.kind = DescriptorKind::Float;
        break;
      case 'J':
        type.kind = DescriptorKind::Long;
        break;
      case 'D':
        type.kind = DescriptorKind::Double;
        break;
      default:
        return std::nullopt;
    }
  }
  if (rank != 0)
    type = {DescriptorKind::Reference, text.substr(0, end)};
  text.remove_prefix(end);
  return type;
}

/**
 * @brief Consume one descriptor type and report parsing success.
 * @param text Borrowed UTF-8 text to compare, validate, or encode.
 * @return True if a complete legal type prefix was consumed.
 * @ingroup implementation
 */
inline std::optional<unsigned> consume_field_type(std::string_view& text) noexcept {
  if (const auto type = parse_field_type(text))
    return type->width();
  return std::nullopt;
}

/**
 * @brief Validate an entire field descriptor with no trailing bytes.
 * @param text Borrowed UTF-8 text to compare, validate, or encode.
 * @return True for one complete non-void field type.
 * @ingroup implementation
 */
inline bool valid_field_descriptor(std::string_view text) noexcept {
  return parse_field_type(text).has_value() && text.empty();
}

/**
 * @brief Parsed return type and accumulated parameter-slot count for a validated method
 * descriptor.
 * @ingroup implementation
 */
struct MethodDescriptor {
  unsigned parameter_slots; ///< Invocation parameter slot count, excluding an implicit receiver.
  DescriptorType return_type; ///< Method result descriptor or verification type.
};

/**
 * @brief Parse parameter types and the return type, visiting each parameter in order.
 * @param text Borrowed UTF-8 text to compare, validate, or encode.
 * @param parameter Callback receiving each parsed parameter type.
 * @return Parameter slots and return type, or nullopt on malformed input or slot overflow.
 * @ingroup implementation
 */
template<typename Parameter>
inline std::optional<MethodDescriptor> parse_method_descriptor(std::string_view text, Parameter parameter) {
  if (!text.starts_with('('))
    return std::nullopt;
  text.remove_prefix(1);
  unsigned slots = 0;
  while (!text.starts_with(')')) {
    const auto type = parse_field_type(text);
    if (!type || type->width() > 255 - slots)
      return std::nullopt;
    slots += type->width();
    parameter(*type);
  }
  text.remove_prefix(1);
  if (text == "V")
    return MethodDescriptor{slots, {DescriptorKind::Void}};
  const auto result = parse_field_type(text);
  if (result && text.empty())
    return MethodDescriptor{slots, *result};
  return std::nullopt;
}

/**
 * @brief Validate a method descriptor and count parameter slots without a receiver.
 * @param text Borrowed UTF-8 text to compare, validate, or encode.
 * @return The slot count, or nullopt for an invalid descriptor.
 * @ingroup implementation
 */
inline std::optional<unsigned> method_parameter_slots(std::string_view text) noexcept {
  if (const auto parsed = parse_method_descriptor(text, [](DescriptorType) {}))
    return parsed->parameter_slots;
  return std::nullopt;
}

/**
 * @brief Validate a JVM method name including initializer spellings.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return True for an ordinary legal name, `<init>`, or `<clinit>`.
 * @ingroup implementation
 */
inline bool valid_method_name(std::string_view name) noexcept {
  return name == "<init>" || name == "<clinit>" ||
         (valid_unqualified_name(name) && name.find_first_of("<>") == std::string_view::npos);
}

} // namespace cafe::detail
