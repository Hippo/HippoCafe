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
import rip.hippo.api.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.api.hippocafe.constantpool.info.impl.{DoubleInfo, FloatInfo, IntegerInfo, LongInfo, NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.api.hippocafe.exception.HippoCafeException
import rip.hippo.api.hippocafe.translation.instruction.impl.{ConstantInstruction, ReferenceInstruction}
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

  def generateConstantPool: ConstantPool = {
    val constantPool = new ConstantPool
    var index = 1

    constantPool.insert(index, new StringInfo(classFile.name, ConstantPoolKind.CLASS))
    index += 1
    constantPool.insert(index, UTF8Info(classFile.name))
    index += 1

    classFile.attributes.foreach(attribute => {
      constantPool.insert(index, UTF8Info(attribute.kind.toString))
      index += 1
    })
    classFile.methods.foreach(methodInfo => {
      constantPool.insert(index, UTF8Info(methodInfo.name))
      index += 1
      constantPool.insert(index, UTF8Info(methodInfo.descriptor))
      index += 1
      /*Type.getMethodParameterTypes(methodInfo.descriptor).filter(_.isObject).foreach(`type` => {
        constantPool.insert(index, UTF8Info(`type`.descriptor))
        index += 1
        constantPool.insert(index, new StringInfo(`type`.descriptor, ConstantPoolKind.CLASS))
        index += 1
      })*/
      methodInfo.instructions.foreach {
        case instruction: ReferenceInstruction =>
          constantPool.insert(index, UTF8Info(instruction.owner))
          index += 1
          constantPool.insert(index, new StringInfo(instruction.owner, ConstantPoolKind.CLASS))
          index += 1
          constantPool.insert(index, UTF8Info(instruction.name))
          index += 1
          constantPool.insert(index, UTF8Info(instruction.descriptor))
          index += 1
          constantPool.insert(index, new NameAndTypeInfo(instruction.name, instruction.descriptor))
          index += 1
          constantPool.insert(index, new ReferenceInfo(
            new StringInfo(instruction.owner, ConstantPoolKind.CLASS),
            new NameAndTypeInfo(instruction.name, instruction.descriptor),
            if (instruction.isMethod) ConstantPoolKind.METHOD_REF else ConstantPoolKind.FIELD_REF))
          index += 1
        case instruction: ConstantInstruction =>
          instruction.constant match { // TODO: finish
            case string: String =>
              constantPool.insert(index, UTF8Info(string))
              index += 1
              constantPool.insert(index, new StringInfo(string, ConstantPoolKind.STRING))
              index += 1
            case int: Int =>
              constantPool.insert(index, IntegerInfo(int))
              index += 1
            case float: Float =>
              constantPool.insert(index, FloatInfo(float))
              index += 1
            case double: Double =>
              constantPool.insert(index, DoubleInfo(double))
              index += 2
            case long: Long =>
              constantPool.insert(index, LongInfo(long))
              index += 2
          }
        case _ =>
      }
      methodInfo.tryCatchBlocks.map(_.catchType).foreach(catchType => {
        constantPool.insert(index, UTF8Info(catchType))
        index += 1
        constantPool.insert(index, new StringInfo(catchType, ConstantPoolKind.CLASS))
      })
      methodInfo.attributes.foreach(attribute => {
        constantPool.insert(index, UTF8Info(attribute.kind.toString))
        index += 1
      })
    })
    classFile.fields.foreach(fieldInfo => {
      constantPool.insert(index, UTF8Info(fieldInfo.name))
      index += 1
      constantPool.insert(index, UTF8Info(fieldInfo.descriptor))
      index += 1
      fieldInfo.attributes.foreach(attribute => {
        constantPool.insert(index, UTF8Info(attribute.kind.toString))
        index += 1
      })
    })

    constantPool
  }
}
