/*
 * MIT License
 *
 * Copyright (c) 2020 Hippo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

package rip.hippo.api.hippocafe.attribute

import rip.hippo.api.hippocafe.attribute

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
object Attribute extends Enumeration {

  private implicit def toValue(name: String): Value = Value(name)

  type Attribute = Value

  val SOURCE_FILE: Attribute = "SourceFile"
  val INNER_CLASSES: Attribute = "InnerClasses"
  val ENCLOSING_METHOD: Attribute = "EnclosingMethod"
  val SOURCE_DEBUG_EXTENSION: Attribute = "SourceDebugExtension"
  val BOOTSTRAP_METHODS: Attribute = "BootstrapMethods"
  val CONSTANT_VALUE: Attribute = "ConstantValue"
  val CODE: Attribute = "Code"
  val EXCEPTIONS: Attribute = "Exceptions"
  val RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS: Attribute = "RuntimeVisibleParameterAnnotations"
  val RUNTIME_INVISIBLE_PARAMETER_ANNOTATIONS: Attribute = "RuntimeInvisibleParameterAnnotations"
  val ANNOTATION_DEFAULT: Attribute = "AnnotationDefault"
  val METHOD_PARAMETERS: Attribute = "MethodParameters"
  val SYNTHETIC: Attribute = "Synthetic"
  val DEPRECATED: Attribute = "Deprecated"
  val SIGNATURE: Attribute = "Signature"
  val RUNTIME_VISIBLE_ANNOTATIONS: Attribute = "RuntimeVisibleAnnotations"
  val RUNTIME_INVISIBLE_ANNOTATIONS: Attribute = "RuntimeInvisibleAnnotations"
  val LINE_NUMBER_TABLE: Attribute = "LineNumberTable"
  val LOCAL_VARIABLE_TABLE: Attribute = "LocalVariableTable"
  val LOCAL_VARIABLE_TYPE_TABLE: Attribute = "LocalVariableTypeTable"
  val STACK_MAP_TABLE: Attribute = "StackMapTable"
  val RUNTIME_VISIBLE_TYPE_ANNOTATIONS: Attribute = "RuntimeVisibleTypeAnnotations"
  val RUNTIME_INVISIBLE_TYPE_ANNOTATIONS: Attribute = "RuntimeInvisibleTypeAnnotations"
  val MODULE: Attribute = "Module"
  val MODULE_PACKAGES: Attribute = "ModulePackages"
  val MODULE_MAIN_CLASS: Attribute = "ModuleMainClass"
  val NEST_HOST: Attribute = "NestHost"
  val NEST_MEMBERS: Attribute = "NestMembers"


  def unknown(name: String): Value = Value(name)


  def fromName(name: String): Option[Attribute] =
    Attribute.values.find(_.toString.equals(name))

}
