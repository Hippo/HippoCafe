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
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.*
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class SimpleInstruction(var bytecodeOpcode: BytecodeOpcode) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val uniqueByte = UniqueByte(bytecodeOpcode.opcode.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    assemblerContext.code += uniqueByte

    if (assemblerContext.calculateMaxes) {
      bytecodeOpcode match {
        case ALOAD_0 | ILOAD_0 | FLOAD_0 | ASTORE_0 | ISTORE_0 | FSTORE_0 => assemblerContext.setMaxLocals(1)
        case ALOAD_1 | ILOAD_1 | FLOAD_1 | ASTORE_1 | ISTORE_1 | FSTORE_1 => assemblerContext.setMaxLocals(2)
        case ALOAD_2 | ILOAD_2 | FLOAD_2 | ASTORE_2 | ISTORE_2 | FSTORE_2 => assemblerContext.setMaxLocals(3)
        case ALOAD_3 | ILOAD_3 | FLOAD_3 | ASTORE_3 | ISTORE_3 | FSTORE_3 => assemblerContext.setMaxLocals(4)
        case LLOAD_0 | DLOAD_0 | LSTORE_0 | DSTORE_0 => assemblerContext.setMaxLocals(2)
        case LLOAD_1 | DLOAD_1 | LSTORE_1 | DSTORE_1 => assemblerContext.setMaxLocals(3)
        case LLOAD_2 | DLOAD_2 | LSTORE_2 | DSTORE_2 => assemblerContext.setMaxLocals(4)
        case LLOAD_3 | DLOAD_3 | LSTORE_3 | DSTORE_3 => assemblerContext.setMaxLocals(5)
        case _ =>
      }
    }
  }

  override def getOpcode: Option[BytecodeOpcode] = Option(bytecodeOpcode)
}
