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

package rip.hippo.hippocafe

import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.analaysis.MaxStackDepthCalculator
import rip.hippo.hippocafe.analaysis.impl.StandardMaxStackDepthCalculator
import rip.hippo.hippocafe.attribute.impl.CodeAttribute
import rip.hippo.hippocafe.attribute.impl.data.ExceptionTableAttributeData
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.constantpool.info.impl._
import rip.hippo.hippocafe.constantpool.info.{ConstantPoolInfo, impl}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, AssemblerFlag}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl._
import rip.hippo.hippocafe.disassembler.instruction.impl.{ConstantInstruction, ReferenceInstruction}
import rip.hippo.hippocafe.util.Type

import java.io.{ByteArrayOutputStream, DataOutputStream}
import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.1, 8/3/20
 * @since 1.0.0
 */
final class ClassWriter(classFile: ClassFile) extends AutoCloseable {

  private val flags = mutable.Set[AssemblerFlag]()
  private val byteOut = new ByteArrayOutputStream()
  private val out = new DataOutputStream(byteOut)
  private var stackDepthCalculator: MaxStackDepthCalculator = new StandardMaxStackDepthCalculator

  def write: Array[Byte] = {
    out.writeInt(0xCAFEBABE)

    out.writeShort(classFile.minorVersion)
    out.writeShort(classFile.majorClassFileVersion.id)

    val removedCodeAttribute = ListBuffer[MethodInfo]()


    val constantPool = classFile.constantPool match {
      case Some(value) => if (classFile.lowLevel) value else generateConstantPool
      case None => generateConstantPool
    }

    var hasCodeAttribute = constantPool.info.filter(_._2.isInstanceOf[UTF8Info]).exists(_._2.asInstanceOf[UTF8Info].value.equals("Code"))
    classFile.methods.foreach(method => {
      if (method.instructions.nonEmpty) {
        if (!hasCodeAttribute) {
          val max = constantPool.info.keys.max
          val index = max + (if (constantPool.info(max).wide) 2 else 1)
          constantPool.insert(index, UTF8Info("Code"))
          hasCodeAttribute = true
        }
        val assemblerContext = new AssemblerContext(flags.toSet)

        if (assemblerContext.calculateMaxes) {
          val defaultSize = Type.getMethodParameterTypes(method.descriptor).map(_.getSize).sum + (if (method.accessFlags.contains(AccessFlag.ACC_STATIC)) 0 else 1)
          val maxStack = stackDepthCalculator.calculateMaxStack(method)
          assemblerContext.setMaxLocals(defaultSize)
          assemblerContext.setMaxStack(maxStack)
        } else {
          assemblerContext.setMaxLocals(method.maxLocals)
          assemblerContext.setMaxStack(method.maxStack)
        }

        method.instructions.foreach(_.assemble(assemblerContext, constantPool))
        assemblerContext.processBranchOffsets()
        val attributes = assemblerContext.assembleMethodAttributes(method)
        val tryCatchBlocks = assemblerContext.assembleTryCatchBlocks(method)

        attributes.foreach(attribute => {
          val info = UTF8Info(attribute.kind.toString)
          var index = 1
          if (constantPool.info.nonEmpty) {
            val max = constantPool.info.keys.max
            index = max + (if (constantPool.info(max).wide) 2 else 1)
          }
          constantPool.info.values.find(_.equals(info)) match {
            case Some(_) =>
            case None =>
              constantPool.insert(index, info)
          }
          attribute.buildConstantPool(constantPool)
        })

        val methodBytecode = assemblerContext.code
        removedCodeAttribute += method

        val codeAttribute = CodeAttribute(
          classFile.isOak,
          assemblerContext.maxStack,
          assemblerContext.maxLocals,
          methodBytecode.length,
          methodBytecode.map(_.byte).toArray,
          tryCatchBlocks.length,
          tryCatchBlocks,
          attributes.length,
          attributes
        )
        codeAttribute.buildConstantPool(constantPool)
        method.attributes += codeAttribute
      }
    })



    out.writeShort(constantPool.info.keys.max + 1)
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

    removedCodeAttribute.foreach(method => {
      method.attributes.filter(_.kind == Attribute.CODE).foreach(method.attributes.subtractOne)
    })

    byteOut.toByteArray
  }

  def generateConstantPool: ConstantPool = {
    val constantPool = if (flags.contains(AssemblerFlag.APPEND_CONSTANT_POOL)) classFile.constantPool.get else new ConstantPool

    def add(info: ConstantPoolInfo): Unit = {
      var index = 1
      if (constantPool.info.nonEmpty) {
        val max = constantPool.info.keys.max
        index = max + (if (constantPool.info(max).wide) 2 else 1)
      }
      constantPool.info.values.find(_.equals(info)) match {
        case Some(_) =>
        case None =>
          constantPool.insert(index, info)
      }
    }

    add(new StringInfo(classFile.name, ConstantPoolKind.CLASS))
    add(UTF8Info(classFile.name))
    add(new StringInfo(classFile.superName, ConstantPoolKind.CLASS))
    add(UTF8Info(classFile.superName))

    classFile.attributes.foreach(attribute => {
      add(UTF8Info(attribute.kind.toString))
      attribute.buildConstantPool(constantPool)
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
            case StringConstant(value) =>
              add(UTF8Info(value))
              add(new StringInfo(value, ConstantPoolKind.STRING))
            case IntegerConstant(value) =>
              add(IntegerInfo(value))
            case FloatConstant(value) =>
              add(FloatInfo(value))
            case DoubleConstant(value) =>
              add(DoubleInfo(value))
            case LongConstant(value) =>
              add(LongInfo(value))
          }
        case _ =>
      }
      methodInfo.tryCatchBlocks.map(_.catchType).foreach(catchType => {
        add(UTF8Info(catchType))
        add(new StringInfo(catchType, ConstantPoolKind.CLASS))
      })
      methodInfo.attributes.foreach(attribute => {
        add(UTF8Info(attribute.kind.toString))
        attribute.buildConstantPool(constantPool)
      })
    })
    classFile.fields.foreach(fieldInfo => {
      add(UTF8Info(fieldInfo.name))
      add(UTF8Info(fieldInfo.descriptor))
      fieldInfo.attributes.foreach(attribute => {
        add(UTF8Info(attribute.kind.toString))
        attribute.buildConstantPool(constantPool)
      })
    })

    constantPool
  }

  def calculateMaxes: ClassWriter = {
    flags += AssemblerFlag.CALCULATE_MAXES
    this
  }

  def generateFrames: ClassWriter = {
    flags += AssemblerFlag.GENERATE_FRAMES
    this
  }

  def appendConstantPool: ClassWriter = {
    flags += AssemblerFlag.APPEND_CONSTANT_POOL
    this
  }

  def setStackCalculator(maxStackDepthCalculator: MaxStackDepthCalculator): ClassWriter = {
    this.stackDepthCalculator = maxStackDepthCalculator
    this
  }

  override def close(): Unit = out.close()

  override def toString: String =
    "ClassWriter(" + classFile + ", " + flags + ")"
}
