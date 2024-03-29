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

package rip.hippo.hippocafe.constantpool.info.impl

import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream
import rip.hippo.hippocafe.constantpool.ConstantPoolKind
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo

/**
 * @author Hippo
 * @version 1.0.1, 8/1/20
 * @since 1.0.0
 */
final case class StringInfo(index: Int, inputKind: ConstantPoolKind) extends ConstantPoolInfo {

  def this(value: String, inputKind: ConstantPoolKind) = {
    this(-1, inputKind)
    this.value = value
  }

  override val kind: ConstantPoolKind = inputKind

  var value: String = _

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(constantPool.findUTF8(value))
  }

  override def readCallback(constantPool: ConstantPool): Unit = {
    value = constantPool.readUTF8(index)
  }

  override def insertIfAbsent(constantPool: ConstantPool): Unit =
    constantPool.insertStringIfAbsent(value, kind)

  override def toString: String =
    "StringInfo(" + (Option(value) match {
      case Some(value) => value
      case None => index
    }) + ", " + inputKind + ")"

  override def equals(obj: Any): Boolean =
    obj match {
      case info: StringInfo => info.value.equals(value) && info.kind == kind
      case _ => false
    }
}
