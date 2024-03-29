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

package rip.hippo.hippocafe.attribute.impl

import java.io.{ByteArrayOutputStream, DataOutputStream}
import rip.hippo.hippocafe.attribute.Attribute.Attribute
import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.attribute.impl.data.ExceptionTableAttributeData
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.impl.StringInfo

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
final case class CodeAttribute(var oak: Boolean,
                               var maxStack: Int,
                               var maxLocals: Int,
                               code: ListBuffer[Byte],
                               var exceptionTableLength: Int,
                               exceptionTable: ListBuffer[ExceptionTableAttributeData],
                               attributes: ListBuffer[AttributeInfo]) extends AttributeInfo {

  override val kind: Attribute = Attribute.CODE


  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    if (oak) {
      out.writeByte(maxStack)
      out.writeByte(maxLocals)
      out.writeShort(code.size)
    } else {
      out.writeShort(maxStack)
      out.writeShort(maxLocals)
      out.writeInt(code.size)
    }
    out.write(code.toArray)
    out.writeShort(exceptionTableLength)
    exceptionTable.foreach(table => {
      out.writeShort(table.startPc)
      out.writeShort(table.endPc)
      out.writeShort(table.handlerPc)
      out.writeShort(constantPool.findString(table.safeCatchType, ConstantPoolKind.CLASS))
    })
    out.writeShort(attributes.size)
    attributes.foreach(attribute => {
      val byteArrayOutputStream = new ByteArrayOutputStream()
      val dataOutputStream = new DataOutputStream(byteArrayOutputStream)
      attribute.write(dataOutputStream, constantPool)
      val attributeData = byteArrayOutputStream.toByteArray
      out.writeShort(constantPool.findUTF8(attribute.kind.toString))
      out.writeInt(attributeData.length)
      out.write(attributeData)
    })
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit = {
    exceptionTable.foreach(_.buildConstantPool(constantPool))
  }
}
