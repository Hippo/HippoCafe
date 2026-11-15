#pragma once

#include <optional>
#include <string_view>

namespace cafe::detail {

inline bool valid_unqualified_name(std::string_view name) noexcept {
  return !name.empty() && name.find_first_of(".;[/") == std::string_view::npos;
}

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

enum class DescriptorKind { Void,
                            Integer,
                            Float,
                            Long,
                            Double,
                            Reference };

struct DescriptorType {
  DescriptorKind kind;
  // The internal name for classes and the full descriptor for arrays.
  std::string_view reference = {};

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

inline std::optional<unsigned> consume_field_type(std::string_view& text) noexcept {
  if (const auto type = parse_field_type(text))
    return type->width();
  return std::nullopt;
}

inline bool valid_field_descriptor(std::string_view text) noexcept {
  return parse_field_type(text).has_value() && text.empty();
}

struct MethodDescriptor {
  unsigned parameter_slots;
  DescriptorType return_type;
};

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

inline std::optional<unsigned> method_parameter_slots(std::string_view text) noexcept {
  if (const auto parsed = parse_method_descriptor(text, [](DescriptorType) {}))
    return parsed->parameter_slots;
  return std::nullopt;
}

inline bool valid_method_name(std::string_view name) noexcept {
  return name == "<init>" || name == "<clinit>" ||
         (valid_unqualified_name(name) && name.find_first_of("<>") == std::string_view::npos);
}

} // namespace cafe::detail
