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

import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant
import rip.hippo.hippocafe.disassembler.instruction.constant.impl._
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}


/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class ConstantInstruction(var constant: Constant[_]) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    constant.insertIfAbsent(constantPool)
    val code = assemblerContext.code
    val index = constant.getConstantPoolIndex(constantPool)

    def writeWide(): Unit = {
      val uniqueByte = UniqueByte(BytecodeOpcode.LDC2_W.id.toByte)
      assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
      assemblerContext.labelQueue.clear()

      assemblerContext.code += uniqueByte
      code += UniqueByte(((index >>> 8) & 0xFF).toByte)
      code += UniqueByte((index & 0xFF).toByte)
    }

    constant match {
      case _: LongConstant =>
        writeWide()
      case _: DoubleConstant =>
        writeWide()
      case _ =>
        if (index > 255) {
          val uniqueByte = UniqueByte(BytecodeOpcode.LDC_W.id.toByte)
          assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
          assemblerContext.labelQueue.clear()

          assemblerContext.code += uniqueByte
          code += UniqueByte(((index >>> 8) & 0xFF).toByte)
          code += UniqueByte((index & 0xFF).toByte)
        } else {
          val uniqueByte = UniqueByte(BytecodeOpcode.LDC.id.toByte)
          assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
          assemblerContext.labelQueue.clear()

          assemblerContext.code += uniqueByte
          code += UniqueByte(index.toByte)
        }
    }
  }
}

