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

import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.{BytecodeOpcode, DLOAD, DSTORE, LLOAD, LSTORE}
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}


/**
 * @author Hippo
 * @version 1.0.0, 8/5/20
 * @since 1.0.0
 */
final case class VariableInstruction(bytecodeOpcode: BytecodeOpcode, index: Int) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val code = assemblerContext.code
    val wide = index > 255
    if (wide) {
      code += BytecodeOpcode.WIDE.id.toByte
    }
    code += bytecodeOpcode.id.toByte
    if (wide) {
      code += (index << 8).toByte
      code += (index & 0xFF).toByte
    } else {
      code += index.toByte
    }

    if (assemblerContext.calculateMaxes) {
      assemblerContext.setMaxLocals(index + ( bytecodeOpcode match {
        case DLOAD | LLOAD | DSTORE | LSTORE => 2
        case _ => 1
      }))
    }
  }
}
