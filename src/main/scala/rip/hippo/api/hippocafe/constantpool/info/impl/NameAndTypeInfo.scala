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

package rip.hippo.api.hippocafe.constantpool.info.impl

import java.io.DataOutputStream

import rip.hippo.api.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.api.hippocafe.constantpool.ConstantPoolKind.ConstantPoolKind
import rip.hippo.api.hippocafe.constantpool.info.ConstantPoolInfo

/**
 * @author Hippo
 * @version 1.0.1, 8/1/20
 * @since 1.0.0
 */
final case class NameAndTypeInfo(nameIndex: Int, descriptorIndex: Int) extends ConstantPoolInfo {
  override val kind: ConstantPoolKind = ConstantPoolKind.NAME_AND_TYPE

  def this(name: String, descriptor: String) {
    this(-1, -1)
    this.name = name;
    this.descriptor = descriptor
  }

  var name: String = _
  var descriptor: String = _

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(constantPool.findUTF8(name))
    out.writeShort(constantPool.findUTF8(descriptor))
  }

  override def readCallback(constantPool: ConstantPool): Unit = {
    name = constantPool.readUTF8(nameIndex)
    descriptor = constantPool.readUTF8(descriptorIndex)
  }

  override def toString: String =
    "NameAndTypeInfo(" + (Option(name) match {
      case Some(value) => value
      case None => nameIndex
    }) + ", " + (Option(descriptor) match {
      case Some(value) => value
      case None => descriptorIndex
    }) + ")"
}
