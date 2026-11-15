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

// Consume one field type and return the number of local/argument slots.
inline std::optional<unsigned> consume_field_type(std::string_view& text) noexcept {
  const auto type = text.find_first_not_of('[');
  if (type == std::string_view::npos || type > 255)
    return std::nullopt;
  text.remove_prefix(type);
  const char tag = text.front();
  text.remove_prefix(1);
  if (tag == 'L') {
    const auto end = text.find(';');
    if (end == std::string_view::npos || !valid_class_name(text.substr(0, end)))
      return std::nullopt;
    text.remove_prefix(end + 1);
  } else if (std::string_view("BCDFIJSZ").find(tag) == std::string_view::npos) {
    return std::nullopt;
  }
  return type == 0 && (tag == 'D' || tag == 'J') ? 2u : 1u;
}

inline bool valid_field_descriptor(std::string_view text) noexcept {
  return consume_field_type(text).has_value() && text.empty();
}

inline std::optional<unsigned> method_parameter_slots(std::string_view text) noexcept {
  if (!text.starts_with('('))
    return std::nullopt;
  text.remove_prefix(1);
  unsigned slots = 0;
  while (!text.starts_with(')')) {
    const auto width = consume_field_type(text);
    if (!width || *width > 255 - slots)
      return std::nullopt;
    slots += *width;
  }
  text.remove_prefix(1);
  if (text != "V" && !valid_field_descriptor(text))
    return std::nullopt;
  return slots;
}

inline bool valid_method_name(std::string_view name) noexcept {
  return name == "<init>" || name == "<clinit>" ||
         (valid_unqualified_name(name) && name.find_first_of("<>") == std::string_view::npos);
}

} // namespace cafe::detail
