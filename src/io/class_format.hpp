#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <cafe/access_flags.hpp>
#include <cafe/attribute_kind.hpp>
#include <cafe/class_version.hpp>
#include <cafe/constant_pool.hpp>

namespace cafe::detail {

enum class CpForm : std::uint8_t { Invalid,
                                   Utf8,
                                   Bits32,
                                   Bits64,
                                   Index,
                                   Pair,
                                   Handle };

struct CpFormat {
  CpForm form;
  std::uint16_t introduced_major = 45;
};

[[nodiscard]] constexpr CpFormat cp_format(CpTag tag) noexcept {
  switch (tag) {
    case CpTag::Utf8:
      return {CpForm::Utf8};
    case CpTag::Integer:
    case CpTag::Float:
      return {CpForm::Bits32};
    case CpTag::Long:
    case CpTag::Double:
      return {CpForm::Bits64};
    case CpTag::Class:
    case CpTag::String:
      return {CpForm::Index};
    case CpTag::Fieldref:
    case CpTag::Methodref:
    case CpTag::InterfaceMethodref:
    case CpTag::NameAndType:
      return {CpForm::Pair};
    case CpTag::MethodHandle:
      return {CpForm::Handle, 51};
    case CpTag::MethodType:
      return {CpForm::Index, 51};
    case CpTag::Dynamic:
      return {CpForm::Pair, 55};
    case CpTag::InvokeDynamic:
      return {CpForm::Pair, 51};
    case CpTag::Module:
    case CpTag::Package:
      return {CpForm::Index, 53};
    default:
      return {CpForm::Invalid};
  }
}

[[nodiscard]] constexpr bool short_code_header(ClassVersion version) noexcept {
  // Historical JVMs read u1/u1/u2 Code limits in class versions up to 45.2.
  return version.major == 45 && version.minor <= 2;
}

[[nodiscard]] constexpr bool is_module(ClassVersion version, std::uint16_t flags) noexcept {
  return version.major >= 53 && has_flag(flags, AccessFlag::Module);
}

[[nodiscard]] constexpr bool dynamic_constant(CpTag tag) noexcept {
  return tag == CpTag::Dynamic || tag == CpTag::InvokeDynamic;
}

[[nodiscard]] constexpr bool loadable_constant(CpTag tag, ClassVersion version) noexcept {
  switch (tag) {
    case CpTag::Integer:
    case CpTag::Float:
    case CpTag::Long:
    case CpTag::Double:
    case CpTag::String:
      return true;
    case CpTag::Class:
      return version.major >= 49;
    case CpTag::MethodType:
    case CpTag::MethodHandle:
      return version.major >= 51;
    case CpTag::Dynamic:
      return version.major >= 55;
    default:
      return false;
  }
}

[[nodiscard]] constexpr CpTag constant_value_tag(std::string_view field_descriptor) noexcept {
  if (field_descriptor == "Ljava/lang/String;")
    return CpTag::String;
  if (field_descriptor.size() != 1)
    return CpTag::Reserved;
  switch (field_descriptor.front()) {
    case 'B':
    case 'C':
    case 'I':
    case 'S':
    case 'Z':
      return CpTag::Integer;
    case 'J':
      return CpTag::Long;
    case 'F':
      return CpTag::Float;
    case 'D':
      return CpTag::Double;
    default:
      return CpTag::Reserved;
  }
}

[[nodiscard]] inline std::string index_path(std::string_view list, std::size_t index) {
  std::string path(list);
  path += '[';
  path += std::to_string(index);
  path += ']';
  return path;
}

[[nodiscard]] std::optional<std::string_view> class_name(const ConstantPool& pool, CpIndex index) noexcept;
[[nodiscard]] const char* class_identity_issue(const ConstantPool& pool, ClassVersion version,
                                               std::uint16_t flags, CpIndex self, CpIndex super) noexcept;
[[nodiscard]] bool class_permits_attribute(AttributeKind kind, ClassVersion version, std::uint16_t class_flags,
                                           bool annotation_element) noexcept;
[[nodiscard]] bool is_annotation_element(std::uint16_t owner_flags, std::uint16_t method_flags,
                                         std::string_view descriptor) noexcept;
[[nodiscard]] const char* cp_reference_issue(const ConstantPool& pool, const CpEntry& entry,
                                             ClassVersion version, bool module) noexcept;

} // namespace cafe::detail
