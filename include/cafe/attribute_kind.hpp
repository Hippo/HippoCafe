/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>

namespace cafe {

/**
 * @brief Recognized JVM attribute names; recognition does not imply a typed payload.
 * @ingroup model
 */
enum class AttributeKind : std::uint8_t {
  Unknown, ///< Unrecognized attribute retained as raw bytes.
  ConstantValue, ///< JVM ConstantValue attribute; the payload may be typed or opaque.
  Code, ///< JVM Code attribute; the payload may be typed or opaque.
  StackMapTable, ///< JVM StackMapTable attribute; the payload may be typed or opaque.
  Exceptions, ///< JVM Exceptions attribute; the payload may be typed or opaque.
  InnerClasses, ///< JVM InnerClasses attribute; the payload may be typed or opaque.
  EnclosingMethod, ///< JVM EnclosingMethod attribute; the payload may be typed or opaque.
  Synthetic, ///< JVM Synthetic attribute; the payload may be typed or opaque.
  Signature, ///< JVM Signature attribute; the payload may be typed or opaque.
  SourceFile, ///< JVM SourceFile attribute; the payload may be typed or opaque.
  SourceDebugExtension, ///< JVM SourceDebugExtension attribute; the payload may be typed or opaque.
  LineNumberTable, ///< JVM LineNumberTable attribute; the payload may be typed or opaque.
  LocalVariableTable, ///< JVM LocalVariableTable attribute; the payload may be typed or opaque.
  LocalVariableTypeTable, ///< JVM LocalVariableTypeTable attribute; the payload may be typed or opaque.
  Deprecated, ///< JVM Deprecated attribute; the payload may be typed or opaque.
  RuntimeVisibleAnnotations, ///< JVM RuntimeVisibleAnnotations attribute; the payload may be typed or opaque.
  RuntimeInvisibleAnnotations, ///< JVM RuntimeInvisibleAnnotations attribute; the payload may be typed or opaque.
  RuntimeVisibleParameterAnnotations, ///< JVM RuntimeVisibleParameterAnnotations attribute; the payload may be typed or opaque.
  RuntimeInvisibleParameterAnnotations, ///< JVM RuntimeInvisibleParameterAnnotations attribute; the payload may be typed or opaque.
  RuntimeVisibleTypeAnnotations, ///< JVM RuntimeVisibleTypeAnnotations attribute; the payload may be typed or opaque.
  RuntimeInvisibleTypeAnnotations, ///< JVM RuntimeInvisibleTypeAnnotations attribute; the payload may be typed or opaque.
  AnnotationDefault, ///< JVM AnnotationDefault attribute; the payload may be typed or opaque.
  BootstrapMethods, ///< JVM BootstrapMethods attribute; the payload may be typed or opaque.
  MethodParameters, ///< JVM MethodParameters attribute; the payload may be typed or opaque.
  Module, ///< JVM Module attribute; the payload may be typed or opaque.
  ModulePackages, ///< JVM ModulePackages attribute; the payload may be typed or opaque.
  ModuleMainClass, ///< JVM ModuleMainClass attribute; the payload may be typed or opaque.
  NestHost, ///< JVM NestHost attribute; the payload may be typed or opaque.
  NestMembers, ///< JVM NestMembers attribute; the payload may be typed or opaque.
  Record, ///< JVM Record attribute; the payload may be typed or opaque.
  PermittedSubclasses, ///< JVM PermittedSubclasses attribute; the payload may be typed or opaque.
};

} // namespace cafe
