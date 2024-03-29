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
final case class ReferenceInfo(classIndex: Int, nameAndTypeIndex: Int, inputKind: ConstantPoolKind) extends ConstantPoolInfo {
  override val kind: ConstantPoolKind = inputKind

  def this(classInfo: StringInfo, nameAndTypeInfo: NameAndTypeInfo, inputKind: ConstantPoolKind) = {
    this(-1, -1, inputKind)
    this.classInfo = classInfo
    this.nameAndTypeInfo = nameAndTypeInfo
  }

  var classInfo: StringInfo = _
  var nameAndTypeInfo: NameAndTypeInfo = _

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    // recompute class index
    val classIndex = constantPool.info.filter {
      case (_, info) => info.equals(classInfo)
      case _ => false
    }.keys.head

    // recompute name and type info index
    val nameAndTypeIndex = constantPool.info.filter {
      case (_, info) => info.equals(nameAndTypeInfo)
      case _ => false
    }.keys.head

    out.writeShort(classIndex)
    out.writeShort(nameAndTypeIndex)
  }

  override def readCallback(constantPool: ConstantPool): Unit = {
    classInfo = constantPool.info(classIndex).asInstanceOf[StringInfo]
    nameAndTypeInfo = constantPool.info(nameAndTypeIndex).asInstanceOf[NameAndTypeInfo]
  }


  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    classInfo.insertIfAbsent(constantPool)
    nameAndTypeInfo.insertIfAbsent(constantPool)
    constantPool.insertIfAbsent(this)
  }

  override def toString: String =
    "ReferenceInfo(" + (Option(classInfo) match {
      case Some(value) => value
      case None => classIndex
    }) + ", " + (Option(nameAndTypeInfo) match {
      case Some(value) => value
      case None => nameAndTypeIndex
    }) + ", " + inputKind + ")"

  override def equals(obj: Any): Boolean =
    obj match {
      case info: ReferenceInfo =>
        info.classInfo.equals(classInfo) && info.nameAndTypeInfo.equals(nameAndTypeInfo)
      case _ => false
    }
}
