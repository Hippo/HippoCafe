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
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.{GOTO, GOTO_W, JSR, JSR_W}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final class LabelInstruction(var debugId: Int = -1) extends Instruction {
  override def toString: String = "Label(" + (if (debugId == -1) "" else debugId) + ")"

  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val indexes = assemblerContext.postProcessBranchIndexes
    val code = assemblerContext.code
    assemblerContext.labelToByteOffset += (this -> code.length)

    if (indexes.contains(this)) {
      val jumpOffsets = indexes(this)
      val offset = code.length
      jumpOffsets.foreach(i => {
        val bytecodeOpcode = BytecodeOpcode.fromOpcode(code(i)).get
        val wide = offset > Short.MaxValue
        def shift(bits: Int): Byte = ((offset >>> bits) & 0xFF).toByte
        val opcode = bytecodeOpcode match {
          case JSR if wide => JSR_W
          case JSR_W if !wide => JSR
          case GOTO if wide => GOTO_W
          case GOTO_W if !wide => GOTO
          case _ => bytecodeOpcode
        }
        code.updated(i, opcode.id.toByte)
        if (wide && (opcode == JSR_W || opcode == GOTO_W)) {
          code.insert(i + 1, shift(24))
          code.insert(i + 2, shift(16))
          code.insert(i + 3, shift(8))
          code.insert(i + 4, shift(0))
        } else {
          code.insert(i + 1, shift(8))
          code.insert(i + 2, shift(0))
        }
      })
    }
  }
}


object LabelInstruction {
  def apply(): LabelInstruction = new LabelInstruction
  def apply(debugId: Int) = new LabelInstruction(debugId)
}
