/**
 * @file
 * @brief Bounded class-file parsing, layout, and serialization.
 * @ingroup implementation
 */

#include "class_format.hpp"

#include "../detail/descriptor.hpp"

namespace cafe::detail {

std::optional<std::string_view> class_name(const ConstantPool& pool, CpIndex index) noexcept {
  const auto* entry = pool.get(index);
  if (!entry || entry->tag != CpTag::Class)
    return std::nullopt;
  const auto text = pool.get_utf8(entry->first);
  if (!text || !valid_class_name(*text))
    return std::nullopt;
  return *text;
}

const char* class_identity_issue(const ConstantPool& pool, ClassVersion version, std::uint16_t flags,
                                 CpIndex self, CpIndex super) noexcept {
  const auto name = class_name(pool, self);
  if (!name)
    return "this_class must name a non-array Class constant";
  if (is_module(version, flags)) {
    if (*name != "module-info" || super || flags != static_cast<std::uint16_t>(AccessFlag::Module))
      return "module identity requires module-info, zero super_class and ACC_MODULE alone";
  } else if (*name == "java/lang/Object") {
    if (super)
      return "Object must have a zero super_class";
  } else {
    const auto parent = class_name(pool, super);
    if (!parent)
      return "super_class must name a non-array Class constant";
    if (has_flag(flags, AccessFlag::Interface) && *parent != "java/lang/Object")
      return "an interface must name Object as super_class";
  }
  return nullptr;
}

bool class_permits_attribute(AttributeKind kind, ClassVersion version, std::uint16_t class_flags,
                             bool annotation_element) noexcept {
  const bool module = is_module(version, class_flags);
  switch (kind) {
    case AttributeKind::Module:
    case AttributeKind::ModulePackages:
    case AttributeKind::ModuleMainClass:
      return module;
    case AttributeKind::InnerClasses:
    case AttributeKind::SourceFile:
    case AttributeKind::SourceDebugExtension:
    case AttributeKind::RuntimeVisibleAnnotations:
    case AttributeKind::RuntimeInvisibleAnnotations:
      return true;
    case AttributeKind::PermittedSubclasses:
      return !module && !has_flag(class_flags, AccessFlag::Final);
    case AttributeKind::AnnotationDefault:
      return !module && annotation_element;
    default:
      return !module;
  }
}

bool is_annotation_element(std::uint16_t owner_flags, std::uint16_t method_flags, std::string_view desc) noexcept {
  return has_flag(owner_flags, AccessFlag::Interface) && has_flag(owner_flags, AccessFlag::Annotation) &&
         has_flag(method_flags, AccessFlag::Abstract) && !has_flag(method_flags, AccessFlag::Static) &&
         desc.starts_with("()") && desc != "()V";
}

namespace {

/**
 * @brief Validate a NameAndType constant for field or method use.
 * @param pool Owning class constant pool used for reference validation.
 * @param index One-based constant-pool reference; the required tag is determined by this
 *   operation.
 * @param referring_tag Constant tag whose reference rules are being validated.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
const char* name_and_type_issue(const ConstantPool& pool, CpIndex index, CpTag referring_tag) noexcept {
  const auto* entry = pool.get(index);
  if (!entry || entry->tag != CpTag::NameAndType)
    return "expected a NameAndType constant";
  const auto name = pool.get_utf8(entry->first);
  const auto descriptor = pool.get_utf8(entry->second);
  if (!name || !descriptor)
    return "NameAndType requires valid Utf8 indices";
  if (referring_tag == CpTag::Fieldref || referring_tag == CpTag::Dynamic) {
    if (!valid_unqualified_name(*name) || !valid_field_descriptor(*descriptor) ||
        (referring_tag == CpTag::Dynamic && (*name == "<init>" || *name == "<clinit>")))
      return "invalid field name or descriptor in NameAndType";
  } else {
    if (!valid_method_name(*name) || *name == "<clinit>" || !method_parameter_slots(*descriptor))
      return "invalid method name or descriptor in NameAndType";
    if (*name == "<init>" && (referring_tag != CpTag::Methodref || !descriptor->ends_with('V')))
      return "invalid instance initializer reference";
  }
  return nullptr;
}

/**
 * @brief Validate a method-handle reference kind, target tag, name, and version rules.
 * @param pool Owning class constant pool used for reference validation.
 * @param entry MethodHandle constant to validate against its referenced member.
 * @param version Exact JVM class-file major and minor version.
 * @return A static explanation, or nullptr when valid.
 * @ingroup implementation
 */
const char* method_handle_issue(const ConstantPool& pool, const CpEntry& entry, ClassVersion version) noexcept {
  const auto* target = pool.get(entry.first);
  if (!target || entry.reference_kind < 1 || entry.reference_kind > 9)
    return "invalid method handle kind or reference index";
  const auto kind = entry.reference_kind;
  if (kind <= 4)
    return target->tag == CpTag::Fieldref ? nullptr : "field handle requires Fieldref";

  const bool interface_allowed = kind == 9 || ((kind == 6 || kind == 7) && version.major >= 52);
  const bool method_allowed = kind != 9;
  if (!((method_allowed && target->tag == CpTag::Methodref) ||
        (interface_allowed && target->tag == CpTag::InterfaceMethodref)))
    return "method handle target tag is not legal for its reference kind";
  const auto* name_and_type = pool.get(target->second);
  if (!name_and_type || name_and_type->tag != CpTag::NameAndType)
    return "method handle target requires a NameAndType constant";
  const auto name = pool.get_utf8(name_and_type->first);
  if (!name || (*name == "<init>") != (kind == 8) || *name == "<clinit>")
    return "method handle target name is not legal for its reference kind";
  const auto descriptor = pool.get_utf8(name_and_type->second);
  const auto slots = descriptor ? method_parameter_slots(*descriptor) : std::nullopt;
  if (!slots || (*slots == 255 && kind != 6))
    return "method handle target has an invalid descriptor or parameter slot count";
  return nullptr;
}

} // namespace

const char* cp_reference_issue(const ConstantPool& pool, const CpEntry& entry,
                               ClassVersion version, bool module) noexcept {
  switch (entry.tag) {
    case CpTag::Class: {
      const auto name = pool.get_utf8(entry.first);
      if (!name || !(name->starts_with('[') ? valid_field_descriptor(*name) : valid_class_name(*name)))
        return "Class constant must name a class or array type using valid Utf8";
      return nullptr;
    }
    case CpTag::String:
      return pool.has(entry.first, CpTag::Utf8) ? nullptr : "String constant must refer to Utf8";
    case CpTag::NameAndType: {
      const auto name = pool.get_utf8(entry.first);
      const auto descriptor = pool.get_utf8(entry.second);
      if (!name || !descriptor)
        return "NameAndType requires valid Utf8 indices";
      const bool valid = descriptor->starts_with('(')
                             ? valid_method_name(*name) && method_parameter_slots(*descriptor).has_value()
                             : valid_unqualified_name(*name) && valid_field_descriptor(*descriptor);
      return valid ? nullptr : "invalid NameAndType name or descriptor";
    }
    case CpTag::Fieldref:
    case CpTag::Methodref:
    case CpTag::InterfaceMethodref:
      if (!pool.has(entry.first, CpTag::Class))
        return "member reference requires a Class constant";
      return name_and_type_issue(pool, entry.second, entry.tag);
    case CpTag::Dynamic:
    case CpTag::InvokeDynamic:
      return name_and_type_issue(pool, entry.second, entry.tag);
    case CpTag::MethodType: {
      const auto descriptor = pool.get_utf8(entry.first);
      return descriptor && method_parameter_slots(*descriptor) ? nullptr : "MethodType requires a valid method descriptor";
    }
    case CpTag::MethodHandle:
      return method_handle_issue(pool, entry, version);
    case CpTag::Module:
    case CpTag::Package:
      if (!module && version.major >= 53)
        return "Module and Package constants require ACC_MODULE";
      if (!pool.get_utf8(entry.first))
        return "Module and Package constants require a valid Utf8 name";
      return nullptr;
    default:
      return nullptr;
  }
}

} // namespace cafe::detail
