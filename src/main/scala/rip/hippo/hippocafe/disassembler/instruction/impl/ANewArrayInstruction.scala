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
import rip.hippo.hippocafe.constantpool.info.impl.{StringInfo, UTF8Info}
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class ANewArrayInstruction(var descriptor: String) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    constantPool.insertStringIfAbsent(descriptor, ConstantPoolKind.CLASS)
    val index = constantPool.findString(descriptor, ConstantPoolKind.CLASS)

    val uniqueByte = UniqueByte(BytecodeOpcode.ANEWARRAY.opcode.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    assemblerContext.code += uniqueByte
    assemblerContext.code += UniqueByte(((index >>> 8) & 0xFF).toByte)
    assemblerContext.code += UniqueByte((index & 0xFF).toByte)
  }

  override def getOpcode: Option[BytecodeOpcode] = Option(BytecodeOpcode.ANEWARRAY)
}
