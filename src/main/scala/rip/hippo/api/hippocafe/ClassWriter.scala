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
import rip.hippo.api.hippocafe.attribute.AttributeInfo
import rip.hippo.api.hippocafe.attribute.impl.LineNumberTableAttribute
import rip.hippo.api.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.api.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.api.hippocafe.constantpool.info.impl.{DoubleInfo, FloatInfo, IntegerInfo, LongInfo, NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.api.hippocafe.exception.HippoCafeException
import rip.hippo.api.hippocafe.disassembler.instruction.impl.{ConstantInstruction, ReferenceInstruction}
import rip.hippo.api.hippocafe.util.Type

/**
 * @author Hippo
 * @version 1.0.0, 8/3/20
 * @since 1.0.0
 */
final class ClassWriter(classFile: ClassFile) {

  private val byteOut = new ByteArrayOutputStream()
  private val out = new DataOutputStream(byteOut)

  def write: Array[Byte] = {
    try {
      out.writeInt(0xCAFEBABE)

      out.writeShort(classFile.minorVersion)
      out.writeShort(classFile.majorClassFileVersion.id)

      val constantPool = classFile.constantPool match {
        case Some(value) => if (classFile.lowLevel) value else {
          val generated = generateConstantPool

          generated
        }
        case None => generateConstantPool
      }
      out.writeShort(constantPool.info.values.size + 1)
      constantPool.info.foreach(entry => {
        out.writeByte(entry._2.kind.id)
        entry._2.write(out, constantPool)
      })

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

  def generateConstantPool: ConstantPool = {
    val constantPool = new ConstantPool
    var index = 1

    def add(info: ConstantPoolInfo, incr: Int = 1): Unit = {
      constantPool.info.values.find(_.equals(info)) match {
        case Some(_) =>
        case None =>
          constantPool.insert(index, info)
          index += incr
      }
    }

    add(new StringInfo(classFile.name, ConstantPoolKind.CLASS))
    add(UTF8Info(classFile.name))

    classFile.attributes.foreach(attribute => {
      add(UTF8Info(attribute.kind.toString))
    })
    classFile.methods.foreach(methodInfo => {
      add(UTF8Info(methodInfo.name))
      add(UTF8Info(methodInfo.descriptor))
      methodInfo.instructions.foreach {
        case instruction: ReferenceInstruction =>
          add(UTF8Info(instruction.owner))
          add(new StringInfo(instruction.owner, ConstantPoolKind.CLASS))
          add(UTF8Info(instruction.name))
          add(UTF8Info(instruction.descriptor))
          add(new NameAndTypeInfo(instruction.name, instruction.descriptor))
          add(new ReferenceInfo(
            new StringInfo(instruction.owner, ConstantPoolKind.CLASS),
            new NameAndTypeInfo(instruction.name, instruction.descriptor),
            if (instruction.isMethod) ConstantPoolKind.METHOD_REF else ConstantPoolKind.FIELD_REF))
        case instruction: ConstantInstruction =>
          instruction.constant match { // TODO: finish
            case string: String =>
              add(UTF8Info(string))
              add(new StringInfo(string, ConstantPoolKind.STRING))
            case int: Int =>
              add(IntegerInfo(int))
            case float: Float =>
              add(FloatInfo(float))
            case double: Double =>
              add(DoubleInfo(double), 2)
            case long: Long =>
              add(LongInfo(long), 2)
          }
        case _ =>
      }
      methodInfo.tryCatchBlocks.map(_.catchType).foreach(catchType => {
        add(UTF8Info(catchType))
        add(new StringInfo(catchType, ConstantPoolKind.CLASS))
      })
      methodInfo.attributes.foreach(attribute => {
        add(UTF8Info(attribute.kind.toString))
      })
    })
    classFile.fields.foreach(fieldInfo => {
      add(UTF8Info(fieldInfo.name))
      add(UTF8Info(fieldInfo.descriptor))
      fieldInfo.attributes.foreach(attribute => {
        add(UTF8Info(attribute.kind.toString))
      })
    })

    constantPool
  }
}
