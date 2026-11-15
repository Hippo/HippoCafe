#pragma once

#include <string_view>

#include <cafe/access_flags.hpp>
#include <cafe/class_version.hpp>

#include "descriptor.hpp"

namespace cafe::detail {

inline bool is_static_method(std::uint16_t flags, std::string_view name) noexcept {
  return has_flag(flags, AccessFlag::Static) || name == "<clinit>";
}

inline bool needs_code(std::uint16_t flags, std::string_view name) noexcept {
  return name == "<clinit>" || (!has_flag(flags, AccessFlag::Native) && !has_flag(flags, AccessFlag::Abstract));
}

inline const char* field_signature_issue(std::string_view name, std::string_view descriptor) noexcept {
  if (!valid_unqualified_name(name))
    return "field name is not a valid unqualified Utf8 name";
  return valid_field_descriptor(descriptor) ? nullptr : "invalid field descriptor";
}

inline const char* method_signature_issue(ClassVersion version, std::uint16_t owner_flags,
                                          std::uint16_t flags, std::string_view name,
                                          std::string_view descriptor) noexcept {
  if (!valid_method_name(name))
    return "method name is not a valid unqualified Utf8 name";
  const auto slots = method_parameter_slots(descriptor);
  if (!slots || (*slots == 255 && !is_static_method(flags, name)))
    return "invalid method descriptor or parameter slot count";
  if (name == "<init>" && (!descriptor.ends_with('V') || has_flag(owner_flags, AccessFlag::Interface) ||
                           has_flag(flags, AccessFlag::Static)))
    return "instance initializer must be a non-static class method returning void";
  if (name == "<clinit>" && (!descriptor.ends_with('V') ||
                             (version.major >= 51 && (descriptor != "()V" || !has_flag(flags, AccessFlag::Static)))))
    return "invalid class initializer signature for the declared version";
  return nullptr;
}

} // namespace cafe::detail
