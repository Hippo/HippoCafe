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

import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.Instruction

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class BranchInstruction(bytecodeOpcode: BytecodeOpcode, label: LabelInstruction) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val code = assemblerContext.code
    if (assemblerContext.labelToByteOffset.contains(label)) {
      val offset = assemblerContext.labelToByteOffset(label)
      val wide = offset > Short.MaxValue
      val opcode = bytecodeOpcode match {
        case JSR if wide => JSR_W
        case JSR_W if !wide => JSR
        case GOTO if wide => GOTO_W
        case GOTO_W if !wide => GOTO
        case _ => bytecodeOpcode
      }
      code += opcode.id.toByte

      def shift(bits: Int): Byte = ((offset >>> bits) & 0xFF).toByte

      if (wide && (opcode == JSR_W || opcode == GOTO_W)) {
        code += shift(24)
        code += shift(16)
        code += shift(8)
        code += shift(0)
      } else {
        code += shift(8)
        code += shift(0)
      }
    } else {
      val indexes = assemblerContext.postProcessBranchIndexes
      if (!indexes.contains(label)) {
        indexes += (label -> ListBuffer())
      }
      indexes(label) += code.length
      code += bytecodeOpcode.id.toByte
    }
  }
}
