/*
 * MIT License
 *
 * Copyright (c) 2021 Hippo
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

package rip.hippo.hippocafe.attribute.impl.data.annotation.target

import scala.language.implicitConversions

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
object AnnotationTargetTypeKind extends Enumeration {

  private implicit def toValue(id: Int): Value = Value(id)
  
  type AnnotationTargetInfoKind = Value

  val PARAMETER_GENERIC_CLASS: AnnotationTargetInfoKind = 0x00
  val PARAMETER_GENERIC_METHOD: AnnotationTargetInfoKind = 0x01
  val EXTENDS: AnnotationTargetInfoKind = 0x10
  val BOUND_GENERIC_CLASS: AnnotationTargetInfoKind = 0x11
  val BOUND_GENERIC_METHOD: AnnotationTargetInfoKind = 0x12
  val FIELD: AnnotationTargetInfoKind = 0x13
  val RETURN_OR_NEW: AnnotationTargetInfoKind = 0x14
  val RECEIVER: AnnotationTargetInfoKind = 0x15
  val FORMAL_PARAMETER: AnnotationTargetInfoKind = 0x16
  val THROWS: AnnotationTargetInfoKind = 0x17

  val LOCAL_VARIABLE: AnnotationTargetInfoKind = 0x40
  val RESOURCE_VARIABLE: AnnotationTargetInfoKind = 0x41
  val EXCEPTION_PARAMETER: AnnotationTargetInfoKind = 0x42
  val INSTANCEOF: AnnotationTargetInfoKind = 0x43
  val NEW: AnnotationTargetInfoKind = 0x44
  val METHOD_REFERENCE_NEW: AnnotationTargetInfoKind = 0x45
  val METHOD_REFERENCE_IDENTIFIER: AnnotationTargetInfoKind = 0x46
  val CAST: AnnotationTargetInfoKind = 0x47
  val GENERIC_CONSTRUCTOR: AnnotationTargetInfoKind = 0x48
  val GENERIC_METHOD_INVOCATION: AnnotationTargetInfoKind = 0x49
  val GENERIC_CONSTRUCTOR_NEW: AnnotationTargetInfoKind = 0x4A
  val GENERIC_METHOD_IDENTIFIER: AnnotationTargetInfoKind = 0x4B

  def fromId(id: Int): Option[AnnotationTargetInfoKind] =
    AnnotationTargetTypeKind.values.find(_.id == id)
}
