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

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.IntegerConstant
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/5/20
 * @since 1.0.0
 */
final case class PushInstruction(var value: Int) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val code = assemblerContext.code
    value match {
      case x if x >= -1 && x < 5 =>
        val uniqueByte = UniqueByte((x + 3).toByte)
        assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
        assemblerContext.labelQueue.clear()

        assemblerContext.code += uniqueByte
      case x if x >= Byte.MinValue && x <= Byte.MaxValue =>
        val uniqueByte = UniqueByte(BytecodeOpcode.BIPUSH.opcode.toByte)
        assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
        assemblerContext.labelQueue.clear()

        assemblerContext.code += uniqueByte
        code += UniqueByte(x.toByte)
      case x if x >= Short.MinValue && x <= Short.MaxValue =>
        val uniqueByte = UniqueByte(BytecodeOpcode.SIPUSH.opcode.toByte)
        assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
        assemblerContext.labelQueue.clear()

        assemblerContext.code += uniqueByte

        code += UniqueByte(((x >>> 8) & 0xFF).toByte)
        code += UniqueByte((x & 0xFF).toByte)
      case _ => ConstantInstruction(IntegerConstant(value)).assemble(assemblerContext, constantPool)
    }
  }

  override def getOpcode: Option[BytecodeOpcode] = Option(value match {
    case x if x >= -1 && x < 5 => BytecodeOpcode.fromOpcode(x + 3).get
    case x if x >= Byte.MinValue && x <= Byte.MaxValue => BytecodeOpcode.BIPUSH
    case x if x >= Short.MinValue && x <= Short.MaxValue => BytecodeOpcode.SIPUSH
    case _ => BytecodeOpcode.LDC
  })
}
