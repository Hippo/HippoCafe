/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

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

/**
 * @brief Wire payload layouts for constant-pool tags.
 * @ingroup implementation
 */
enum class CpForm : std::uint8_t {
  Invalid, ///< Unknown or invalid constant-pool wire form.
  Utf8, ///< u2-length-prefixed modified UTF-8 payload.
  Bits32, ///< Four-byte numeric bit payload.
  Bits64, ///< Eight-byte numeric bit payload occupying two pool slots.
  Index, ///< One u2 reference index.
  Pair, ///< Two u2 reference/ordinal fields.
  Handle, ///< u1 method-handle kind followed by a u2 reference index.
};

/**
 * @brief Constant-pool wire payload form and minimum class-file major version.
 * @ingroup implementation
 */
struct CpFormat {
  CpForm form; ///< Constant-pool payload layout used by the reader and writer.
  std::uint16_t introduced_major = 45; ///< First ordinary class-file major supporting this format feature.
};

/**
 * @brief Describe a constant tag's wire fields and introduction version.
 * @param tag Required constant-pool tag.
 * @return The wire form, or CpForm::Invalid for an unknown tag.
 * @ingroup implementation
 */
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

/**
 * @brief Select the historical u1/u1/u2 Code header.
 * @param version Exact JVM class-file major and minor version.
 * @return True for class-file version 45 with minor at most 2.
 * @ingroup implementation
 */
[[nodiscard]] constexpr bool short_code_header(ClassVersion version) noexcept {
  // Historical JVMs read u1/u1/u2 Code limits in class versions up to 45.2.
  return version.major == 45 && version.minor <= 2;
}

/**
 * @brief Recognize ACC_MODULE only where the class-file version permits modules.
 * @param version Exact JVM class-file major and minor version.
 * @param flags JVM access bit mask for the declaration.
 * @return True for a module declaration.
 * @ingroup implementation
 */
[[nodiscard]] constexpr bool is_module(ClassVersion version, std::uint16_t flags) noexcept {
  return version.major >= 53 && has_flag(flags, AccessFlag::Module);
}

/**
 * @brief Identify constants whose first field is a bootstrap-table ordinal.
 * @param tag Required constant-pool tag.
 * @return True for Dynamic and InvokeDynamic.
 * @ingroup implementation
 */
[[nodiscard]] constexpr bool dynamic_constant(CpTag tag) noexcept {
  return tag == CpTag::Dynamic || tag == CpTag::InvokeDynamic;
}

/**
 * @brief Check a constant tag's availability as a loadable value in this version.
 * @param tag Required constant-pool tag.
 * @param version Exact JVM class-file major and minor version.
 * @return True when supported; this does not check ldc category width.
 * @ingroup implementation
 */
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

/**
 * @brief Select the constant tag required by a field's ConstantValue attribute.
 * @param field_descriptor Complete JVM field descriptor.
 * @return The required tag, or Reserved for a descriptor without a legal ConstantValue
 *   representation.
 * @ingroup implementation
 */
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

/**
 * @brief Append a zero-based table index to a diagnostic path.
 * @param list Diagnostic table-name prefix.
 * @param index Zero-based index in the corresponding table or slot array.
 * @return An owned path of the form list[index].
 * @ingroup implementation
 */
[[nodiscard]] inline std::string index_path(std::string_view list, std::size_t index) {
  std::string path(list);
  path += '[';
  path += std::to_string(index);
  path += ']';
  return path;
}

/**
 * @brief Resolve a Class constant through its validated UTF-8 name.
 * @param pool Owning class constant pool used for reference validation.
 * @param index One-based constant-pool reference; the required tag is determined by this
 *   operation.
 * @return A borrowed class-name view, or nullopt for an invalid reference.
 * @ingroup implementation
 */
[[nodiscard]] std::optional<std::string_view> class_name(const ConstantPool& pool, CpIndex index) noexcept;
/**
 * @brief Validate this/superclass references and Object/module identity rules.
 * @param pool Owning class constant pool used for reference validation.
 * @param version Exact JVM class-file major and minor version.
 * @param flags JVM access bit mask for the declaration.
 * @param self One-based constant-pool reference; the required tag is determined by this operation.
 * @param super One-based constant-pool reference; the required tag is determined by this
 *   operation.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
[[nodiscard]] const char* class_identity_issue(const ConstantPool& pool, ClassVersion version,
                                               std::uint16_t flags, CpIndex self, CpIndex super) noexcept;
/**
 * @brief Check class-specific restrictions beyond the registry's general location rules.
 * @param kind Payload or error discriminator.
 * @param version Exact JVM class-file major and minor version.
 * @param class_flags JVM access mask of the owning class.
 * @param annotation_element Whether the owning method is an eligible annotation element.
 * @return True when the declaration permits the attribute.
 * @ingroup implementation
 */
[[nodiscard]] bool class_permits_attribute(AttributeKind kind, ClassVersion version, std::uint16_t class_flags,
                                           bool annotation_element) noexcept;
/**
 * @brief Identify a method eligible for an AnnotationDefault attribute.
 * @param owner_flags JVM access mask of the owning class.
 * @param method_flags JVM access mask of the method.
 * @param desc Borrowed descriptor text consumed by parsing.
 * @return True when its declaration has the required annotation-element shape.
 * @ingroup implementation
 */
[[nodiscard]] bool is_annotation_element(std::uint16_t owner_flags, std::uint16_t method_flags,
                                         std::string_view desc) noexcept;
/**
 * @brief Validate a constant's cross-references, names, and version restrictions.
 * @param pool Owning class constant pool used for reference validation.
 * @param entry Constant whose cross-references and version rules are validated.
 * @param version Exact JVM class-file major and minor version.
 * @param module Whether the owner is a module declaration.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
[[nodiscard]] const char* cp_reference_issue(const ConstantPool& pool, const CpEntry& entry,
                                             ClassVersion version, bool module) noexcept;

} // namespace cafe::detail
