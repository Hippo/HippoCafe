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

package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.{BytecodeOpcode, INVOKEINTERFACE, INVOKESPECIAL, INVOKESTATIC, INVOKEVIRTUAL}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.util.Type

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class ReferenceInstruction(var bytecodeOpcode: BytecodeOpcode, var owner: String, var name: String, var descriptor: String) extends Instruction {
  def isMethod: Boolean = bytecodeOpcode match {
    case INVOKEVIRTUAL | INVOKESTATIC | INVOKEINTERFACE | INVOKESPECIAL => true
    case _ => false
  }
  def isField: Boolean = !isMethod

  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val code = assemblerContext.code
    var index = -1
    var refIndex = -1
    constantPool.info
      .filter(_._2.isInstanceOf[ReferenceInfo])
      .filter(_._2.asInstanceOf[ReferenceInfo].classInfo != null)
      .filter(_._2.asInstanceOf[ReferenceInfo].nameAndTypeInfo != null)
      .filter(pair => {
        val referenceInfo = pair._2.asInstanceOf[ReferenceInfo]
        val nameAndType = referenceInfo.nameAndTypeInfo
        referenceInfo.classInfo != null &&
          referenceInfo.classInfo.value != null &&
          referenceInfo.classInfo.value.equals(owner) &&
          nameAndType.name != null &&
          nameAndType.name.equals(name) &&
          nameAndType.descriptor != null &&
          nameAndType.descriptor.equals(descriptor)
      })
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      def add(info: ConstantPoolInfo): Unit = {
        constantPool.info.values.find(_.equals(info)) match {
          case Some(_) =>
          case None =>
            constantPool.insert(index, info)
            index += 1
        }
      }
      add(UTF8Info(owner))
      add(new StringInfo(owner, ConstantPoolKind.CLASS))
      add(UTF8Info(name))
      add(UTF8Info(descriptor))
      add(new NameAndTypeInfo(name, descriptor))
      refIndex = index
      add(new ReferenceInfo(
        new StringInfo(owner, ConstantPoolKind.CLASS),
        new NameAndTypeInfo(name, descriptor),
        if (isMethod) ConstantPoolKind.METHOD_REF else ConstantPoolKind.FIELD_REF))
      
    } else refIndex = index
    code += bytecodeOpcode.id.toByte
    code += (refIndex >>> 8).toByte
    code += (refIndex & 0xFF).toByte

    if (bytecodeOpcode == INVOKEINTERFACE) {
      code += Type.getMethodParameterTypes(descriptor).map(_.getSize).sum.toByte
      code += 0
    }
  }
}
