#pragma once

#include <cstdint>

namespace cafe {

enum class AttributeKind : std::uint8_t {
  Unknown,
  ConstantValue,
  Code,
  StackMapTable,
  Exceptions,
  InnerClasses,
  EnclosingMethod,
  Synthetic,
  Signature,
  SourceFile,
  SourceDebugExtension,
  LineNumberTable,
  LocalVariableTable,
  LocalVariableTypeTable,
  Deprecated,
  RuntimeVisibleAnnotations,
  RuntimeInvisibleAnnotations,
  RuntimeVisibleParameterAnnotations,
  RuntimeInvisibleParameterAnnotations,
  RuntimeVisibleTypeAnnotations,
  RuntimeInvisibleTypeAnnotations,
  AnnotationDefault,
  BootstrapMethods,
  MethodParameters,
  Module,
  ModulePackages,
  ModuleMainClass,
  NestHost,
  NestMembers,
  Record,
  PermittedSubclasses,
};

} // namespace cafe
