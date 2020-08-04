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

package rip.hippo.api.hippocafe

import java.io.{ByteArrayOutputStream, DataOutputStream}

import rip.hippo.api.hippocafe.access.AccessFlag
import rip.hippo.api.hippocafe.attribute.impl.LineNumberTableAttribute
import rip.hippo.api.hippocafe.constantpool.ConstantPool
import rip.hippo.api.hippocafe.exception.HippoCafeException

/**
 * @author Hippo
 * @version 1.0.0, 8/3/20
 * @since 1.0.0
 */
final class ClassWriter(classFile: ClassFile) {

  private val byteOut = new ByteArrayOutputStream()
  private val out = new DataOutputStream(byteOut)

  def write: Array[Byte] = { //todo: security checks to ensure indices are valid
    try {
      out.writeInt(0xCAFEBABE)

      out.writeShort(classFile.minorVersion)
      out.writeShort(classFile.majorClassFileVersion.id)

      val constantPool = classFile.constantPool match {
        case Some(value) =>
          out.writeShort(value.info.values.size + 1)
          value.info.foreach(entry => {
            out.writeByte(entry._2.kind.id)
            entry._2.write(out, value)
          })
          value
        case None => throw new HippoCafeException("Unable to write class without constant pool.") // todo: build new constant pool
      }

      out.writeShort(AccessFlag.toMask(classFile.access: _*))
      out.writeShort(constantPool.findString(classFile.name))
      out.writeShort(constantPool.findString(classFile.superName))

      out.writeShort(classFile.interfaces.size)
      classFile.interfaces.foreach(interface => out.writeShort(constantPool.findString(interface)))

      out.writeShort(classFile.fields.size)
      classFile.fields.foreach(field => {
        out.writeShort(AccessFlag.toMask(field.accessFlags: _*))
        out.writeShort(constantPool.findUTF8(field.name))
        out.writeShort(constantPool.findUTF8(field.descriptor))
        out.writeShort(field.attributes.size)
        field.attributes.foreach(attribute => {
          val byteArrayOutputStream = new ByteArrayOutputStream()
          val dataOutputStream = new DataOutputStream(byteArrayOutputStream)
          attribute.write(dataOutputStream, constantPool)
          val attributeData = byteArrayOutputStream.toByteArray

          out.writeShort(constantPool.findUTF8(attribute.kind.toString))
          out.writeInt(attributeData.length)
          out.write(attributeData)

          dataOutputStream.close()
        })
      })

      out.writeShort(classFile.methods.size)
      classFile.methods.foreach(method => {
        out.writeShort(AccessFlag.toMask(method.accessFlags: _*))
        out.writeShort(constantPool.findUTF8(method.name))
        out.writeShort(constantPool.findUTF8(method.descriptor))
        out.writeShort(method.attributes.size)

        method.attributes.foreach(attribute => {
          val byteArrayOutputStream = new ByteArrayOutputStream()
          val dataOutputStream = new DataOutputStream(byteArrayOutputStream)
          attribute.write(dataOutputStream, constantPool)
          val attributeData = byteArrayOutputStream.toByteArray

          out.writeShort(constantPool.findUTF8(attribute.kind.toString))
          out.writeInt(attributeData.length)
          out.write(attributeData)

          dataOutputStream.close()
        })
      })

      out.writeShort(classFile.attributes.size)
      classFile.attributes.foreach(attribute => {
        val byteArrayOutputStream = new ByteArrayOutputStream()
        val dataOutputStream = new DataOutputStream(byteArrayOutputStream)
        attribute.write(dataOutputStream, constantPool)
        val attributeData = byteArrayOutputStream.toByteArray

        out.writeShort(constantPool.findUTF8(attribute.kind.toString))
        out.writeInt(attributeData.length)
        out.write(attributeData)

        dataOutputStream.close()
      })

      byteOut.toByteArray
    } finally out.close()
  }
}
