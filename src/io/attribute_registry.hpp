#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <cafe/attribute_kind.hpp>
#include <cafe/class_version.hpp>

namespace cafe::detail {

enum class AttributeContext : std::uint8_t {
  Class = 0x01,
  Field = 0x02,
  Method = 0x04,
  Code = 0x08,
  RecordComponent = 0x10
};

struct AttributeSpec {
  std::string_view name;
  AttributeKind kind;
  std::uint16_t introduced_major;
  std::uint8_t contexts;
  bool repeatable = false;
  std::uint16_t preview_major = 0;

  [[nodiscard]] constexpr bool version_applies(ClassVersion version) const noexcept {
    return version.major >= introduced_major ||
           (preview_major != 0 && version.major >= preview_major && version.minor == 65535);
  }

  [[nodiscard]] constexpr bool context_applies(AttributeContext context) const noexcept {
    return (contexts & std::to_underlying(context)) != 0;
  }

  [[nodiscard]] constexpr bool applicable_to(ClassVersion version, AttributeContext context) const noexcept {
    return version_applies(version) && context_applies(context);
  }
};

inline constexpr auto CLASS_CONTEXT = std::to_underlying(AttributeContext::Class);
inline constexpr auto FIELD_CONTEXT = std::to_underlying(AttributeContext::Field);
inline constexpr auto METHOD_CONTEXT = std::to_underlying(AttributeContext::Method);
inline constexpr auto CODE_CONTEXT = std::to_underlying(AttributeContext::Code);
inline constexpr auto RECORD_COMPONENT_CONTEXT = std::to_underlying(AttributeContext::RecordComponent);
inline constexpr std::uint8_t MEMBER_CONTEXTS = CLASS_CONTEXT | FIELD_CONTEXT | METHOD_CONTEXT;
inline constexpr std::uint8_t DECLARATION_CONTEXTS = MEMBER_CONTEXTS | RECORD_COMPONENT_CONTEXT;

// Includes the historical 45.0–45.2 variants and the record and sealed-class previews.
// JVMS §4.7, https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-4.html#jvms-4.7
inline constexpr std::array ATTRIBUTE_SPECS{
    AttributeSpec{"AnnotationDefault", AttributeKind::AnnotationDefault, 49, METHOD_CONTEXT},
    AttributeSpec{"BootstrapMethods", AttributeKind::BootstrapMethods, 51, CLASS_CONTEXT},
    AttributeSpec{"Code", AttributeKind::Code, 45, METHOD_CONTEXT},
    AttributeSpec{"ConstantValue", AttributeKind::ConstantValue, 45, FIELD_CONTEXT},
    AttributeSpec{"Deprecated", AttributeKind::Deprecated, 45, MEMBER_CONTEXTS, true},
    AttributeSpec{"EnclosingMethod", AttributeKind::EnclosingMethod, 49, CLASS_CONTEXT},
    AttributeSpec{"Exceptions", AttributeKind::Exceptions, 45, METHOD_CONTEXT},
    AttributeSpec{"InnerClasses", AttributeKind::InnerClasses, 45, CLASS_CONTEXT},
    AttributeSpec{"LineNumberTable", AttributeKind::LineNumberTable, 45, CODE_CONTEXT, true},
    AttributeSpec{"LocalVariableTable", AttributeKind::LocalVariableTable, 45, CODE_CONTEXT, true},
    AttributeSpec{"LocalVariableTypeTable", AttributeKind::LocalVariableTypeTable, 49, CODE_CONTEXT, true},
    AttributeSpec{"MethodParameters", AttributeKind::MethodParameters, 52, METHOD_CONTEXT},
    AttributeSpec{"Module", AttributeKind::Module, 53, CLASS_CONTEXT},
    AttributeSpec{"ModuleMainClass", AttributeKind::ModuleMainClass, 53, CLASS_CONTEXT},
    AttributeSpec{"ModulePackages", AttributeKind::ModulePackages, 53, CLASS_CONTEXT},
    AttributeSpec{"NestHost", AttributeKind::NestHost, 55, CLASS_CONTEXT},
    AttributeSpec{"NestMembers", AttributeKind::NestMembers, 55, CLASS_CONTEXT},
    AttributeSpec{"PermittedSubclasses", AttributeKind::PermittedSubclasses, 61, CLASS_CONTEXT, false, 59},
    AttributeSpec{"Record", AttributeKind::Record, 60, CLASS_CONTEXT, false, 58},
    AttributeSpec{"RuntimeInvisibleAnnotations", AttributeKind::RuntimeInvisibleAnnotations, 49, DECLARATION_CONTEXTS},
    AttributeSpec{"RuntimeInvisibleParameterAnnotations", AttributeKind::RuntimeInvisibleParameterAnnotations, 49, METHOD_CONTEXT},
    AttributeSpec{"RuntimeInvisibleTypeAnnotations", AttributeKind::RuntimeInvisibleTypeAnnotations, 52, DECLARATION_CONTEXTS | CODE_CONTEXT},
    AttributeSpec{"RuntimeVisibleAnnotations", AttributeKind::RuntimeVisibleAnnotations, 49, DECLARATION_CONTEXTS},
    AttributeSpec{"RuntimeVisibleParameterAnnotations", AttributeKind::RuntimeVisibleParameterAnnotations, 49, METHOD_CONTEXT},
    AttributeSpec{"RuntimeVisibleTypeAnnotations", AttributeKind::RuntimeVisibleTypeAnnotations, 52, DECLARATION_CONTEXTS | CODE_CONTEXT},
    AttributeSpec{"Signature", AttributeKind::Signature, 49, DECLARATION_CONTEXTS},
    AttributeSpec{"SourceDebugExtension", AttributeKind::SourceDebugExtension, 49, CLASS_CONTEXT},
    AttributeSpec{"SourceFile", AttributeKind::SourceFile, 45, CLASS_CONTEXT},
    AttributeSpec{"StackMapTable", AttributeKind::StackMapTable, 50, CODE_CONTEXT},
    AttributeSpec{"Synthetic", AttributeKind::Synthetic, 45, MEMBER_CONTEXTS, true},
};

static_assert(std::ranges::is_sorted(ATTRIBUTE_SPECS, {}, &AttributeSpec::name));

[[nodiscard]] inline const AttributeSpec* find_attribute(std::string_view name) noexcept {
  const auto found = std::ranges::lower_bound(ATTRIBUTE_SPECS, name, {}, &AttributeSpec::name);
  return found != ATTRIBUTE_SPECS.end() && found->name == name ? &*found : nullptr;
}

} // namespace cafe::detail
