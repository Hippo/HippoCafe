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

package rip.hippo.hippocafe.access

import scala.collection.mutable

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
object AccessFlag extends Enumeration {

  private val lookup = mutable.Map[Int,AccessFlag]()

  protected implicit class MaskVal(val mask: Int) extends super.Val {
    lookup += (mask -> this)
  }

  type AccessFlag = MaskVal

  val ACC_PUBLIC: AccessFlag = 0x001
  val ACC_FINAL: AccessFlag = 0x0010
  val ACC_SUPER: AccessFlag = 0x0020
  val ACC_INTERFACE: AccessFlag = 0x0200
  val ACC_ABSTRACT: AccessFlag = 0x0400
  val ACC_SYNTHETIC: AccessFlag = 0x1000
  val ACC_ANNOTATION: AccessFlag = 0x2000
  val ACC_ENUM: AccessFlag = 0x4000
  val ACC_MODULE: AccessFlag = 0x8000
  val ACC_PRIVATE: AccessFlag = 0x0002
  val ACC_STATIC: AccessFlag = 0x0008
  val ACC_PROTECTED: AccessFlag = 0x0004
  val ACC_VOLATILE: AccessFlag = 0x0040
  val ACC_TRANSIENT: AccessFlag = 0x0080
  val ACC_STRICT: AccessFlag = 0x0800
  val ACC_BRIDGE: AccessFlag = 0x0040
  val ACC_VARARGS: AccessFlag = 0x0080
  val ACC_NATIVE: AccessFlag = 0x0100
  val ACC_SYNCHRONIZED: AccessFlag = 0x0020

  def fromMask(mask: Int): Seq[AccessFlag] =
    lookup.filter(entry => (entry._1 & mask) != 0).values.toSeq

  def toMask(accessFlags: AccessFlag*): Int =
    if (accessFlags.isEmpty) 0 else accessFlags.map(_.mask).reduceLeft(_ | _)
}
