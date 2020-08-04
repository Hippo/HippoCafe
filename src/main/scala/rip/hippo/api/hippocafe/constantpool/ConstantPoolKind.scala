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

package rip.hippo.api.hippocafe.constantpool


/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
object ConstantPoolKind extends Enumeration {


  private implicit def toValue(id: Int): Value = Value(id)

  type ConstantPoolKind = Value

  val CLASS: ConstantPoolKind = 7
  val FIELD_REF: ConstantPoolKind = 9
  val METHOD_REF: ConstantPoolKind = 10
  val INTERFACE_METHOD_REF: ConstantPoolKind = 11
  val STRING: ConstantPoolKind = 8
  val INTEGER: ConstantPoolKind =  3
  val FLOAT: ConstantPoolKind = 4
  val LONG: ConstantPoolKind = 5
  val DOUBLE: ConstantPoolKind = 6
  val NAME_AND_TYPE: ConstantPoolKind = 12
  val UTF8: ConstantPoolKind = 1
  val METHOD_HANDLE: ConstantPoolKind = 15
  val METHOD_TYPE: ConstantPoolKind = 16
  val DYNAMIC: ConstantPoolKind = 17
  val INVOKE_DYNAMIC: ConstantPoolKind = 18
  val MODULE: ConstantPoolKind = 19
  val PACKAGE: ConstantPoolKind = 20


  def fromTag(tag: Int): Option[ConstantPoolKind] =
    ConstantPoolKind.values.find(_.id == tag)

}
