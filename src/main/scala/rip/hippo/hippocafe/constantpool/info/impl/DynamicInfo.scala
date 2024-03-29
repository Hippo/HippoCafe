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

import rip.hippo.hippocafe.attribute.impl.data.BootstrapMethodsAttributeData

import java.io.DataOutputStream
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.ConstantPoolKind
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo

/**
 * @author Hippo
 * @version 1.0.1, 8/1/20
 * @since 1.0.0
 */
final case class DynamicInfo(inputKind: ConstantPoolKind, var bsmAttributeIndex: Int, nameAndTypeIndex: Int) extends ConstantPoolInfo {
  override val kind: ConstantPoolKind = inputKind

  def this (inputKind: ConstantPoolKind, bootstrapMethodsAttributeData: BootstrapMethodsAttributeData, nameAndTypeInfo: NameAndTypeInfo) = {
    this(inputKind, -1, -1)
    this.bootstrapMethodsAttributeData = bootstrapMethodsAttributeData
    this.nameAndTypeInfo = nameAndTypeInfo
  }

  var bootstrapMethodsAttributeData: BootstrapMethodsAttributeData = _
  var nameAndTypeInfo: NameAndTypeInfo = _

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {

    out.writeShort(bsmAttributeIndex)

    // recompute name and type info index
    val nameAndTypeIndex = constantPool.info.filter {
      case (_, info) => info.equals(nameAndTypeInfo)
      case _ => false
    }.keys.head

    out.writeShort(nameAndTypeIndex)
  }

  override def readCallback(constantPool: ConstantPool): Unit = {
    nameAndTypeInfo = constantPool.info(nameAndTypeIndex).asInstanceOf[NameAndTypeInfo]
  }

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    nameAndTypeInfo.insertIfAbsent(constantPool)
    bootstrapMethodsAttributeData.bootstrapMethodRef.insertIfAbsent(constantPool)
    bootstrapMethodsAttributeData.bootstrapArguments.foreach(_.insertIfAbsent(constantPool))
    constantPool.insertIfAbsent(this)
  }

  override def toString: String = {
    "DynamicInfo(" + inputKind + ", " +
      (Option(bootstrapMethodsAttributeData) match {
      case Some(value) => value
      case None => bsmAttributeIndex
    }) + ", " +
      (Option(nameAndTypeInfo) match {
        case Some(value) => value
        case None => nameAndTypeIndex
      }) + ")"
  }

  override def equals(obj: Any): Boolean = {
    obj match {
      case other: DynamicInfo =>
        val bsmData = Option(bootstrapMethodsAttributeData) match {
          case Some(value) => value
          case None => return false
        }
        val nameAndType = Option(nameAndTypeInfo) match {
          case Some(value) => value
          case None => return false
        }

        val otherBsmData = Option(other.bootstrapMethodsAttributeData) match {
          case Some(value) => value
          case None => return false
        }
        val otherNameAndType = Option(other.nameAndTypeInfo) match {
          case Some(value) => value
          case None => return false
        }

        inputKind.equals(other.inputKind) && bsmData.equals(otherBsmData) && nameAndType.equals(otherNameAndType)
      case _ => false
    }
  }
}
