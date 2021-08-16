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

package rip.hippo.hippocafe.constantpool


/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
enum ConstantPoolKind(val tag: Int) {

  case CLASS extends ConstantPoolKind(7)
  case FIELD_REF extends ConstantPoolKind(9)
  case METHOD_REF extends ConstantPoolKind(10)
  case INTERFACE_METHOD_REF extends ConstantPoolKind(11)
  case STRING extends ConstantPoolKind(8)
  case INTEGER extends ConstantPoolKind( 3)
  case FLOAT extends ConstantPoolKind(4)
  case LONG extends ConstantPoolKind(5)
  case DOUBLE extends ConstantPoolKind(6)
  case NAME_AND_TYPE extends ConstantPoolKind(12)
  case UTF8 extends ConstantPoolKind(1)
  case METHOD_HANDLE extends ConstantPoolKind(15)
  case METHOD_TYPE extends ConstantPoolKind(16)
  case DYNAMIC extends ConstantPoolKind(17)
  case INVOKE_DYNAMIC extends ConstantPoolKind(18)
  case MODULE extends ConstantPoolKind(19)
  case PACKAGE extends ConstantPoolKind(20)

}

object ConstantPoolKind {
  def fromTag(tag: Int): Option[ConstantPoolKind] =
    ConstantPoolKind.values.find(_.tag == tag)
}
