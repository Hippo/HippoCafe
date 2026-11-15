/**
 * @file
 * @brief Shared descriptor and JVM member validation helpers.
 * @ingroup implementation
 */

#pragma once

#include <string_view>

#include <cafe/access_flags.hpp>
#include <cafe/class_version.hpp>

#include "descriptor.hpp"

namespace cafe::detail {

/**
 * @brief Determine whether a method has no implicit receiver, including class initializers.
 * @param flags JVM access bit mask for the declaration.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return True for ACC_STATIC or `<clinit>`.
 * @ingroup implementation
 */
inline bool is_static_method(std::uint16_t flags, std::string_view name) noexcept {
  return has_flag(flags, AccessFlag::Static) || name == "<clinit>";
}

/**
 * @brief Determine whether a declaration requires a Code attribute.
 * @param flags JVM access bit mask for the declaration.
 * @param name Borrowed JVM name in modified UTF-8.
 * @return True for a class initializer or a non-native, non-abstract method.
 * @ingroup implementation
 */
inline bool needs_code(std::uint16_t flags, std::string_view name) noexcept {
  return name == "<clinit>" || (!has_flag(flags, AccessFlag::Native) && !has_flag(flags, AccessFlag::Abstract));
}

/**
 * @brief Validate field name and descriptor syntax.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param descriptor JVM field or method descriptor, or its UTF-8 constant-pool index.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
inline const char* field_signature_issue(std::string_view name, std::string_view descriptor) noexcept {
  if (!valid_unqualified_name(name))
    return "field name is not a valid unqualified Utf8 name";
  return valid_field_descriptor(descriptor) ? nullptr : "invalid field descriptor";
}

/**
 * @brief Validate method names, descriptors, receiver-slot limits, and initializer rules.
 * @param version Exact JVM class-file major and minor version.
 * @param owner_flags JVM access mask of the owning class.
 * @param flags JVM access bit mask for the declaration.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param descriptor JVM field or method descriptor, or its UTF-8 constant-pool index.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
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
