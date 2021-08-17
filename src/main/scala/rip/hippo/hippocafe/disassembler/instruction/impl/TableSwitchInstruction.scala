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
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, PreprocessedBranch, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class TableSwitchInstruction(var default: LabelInstruction, var low: Int, var high: Int) extends Instruction {
  val table: ListBuffer[LabelInstruction] = ListBuffer[LabelInstruction]()

  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    def shift(value: Int, bits: Int): UniqueByte = UniqueByte(((value >>> bits) & 0xFF).toByte)

    val code = assemblerContext.code
    val uniqueByte = UniqueByte(BytecodeOpcode.TABLESWITCH.opcode.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    assemblerContext.preprocessedTableSwitch += (this -> uniqueByte)
    code += uniqueByte
    (0 until 4).foreach(_ => code += UniqueByte(0))
    assemblerContext.switchPadding += (code(code.indexOf(uniqueByte) + 1) -> (-code.length & 3))

    code += shift(low, 24)
    code += shift(low, 16)
    code += shift(low, 8)
    code += shift(low, 0)
    code += shift(high, 24)
    code += shift(high, 16)
    code += shift(high, 8)
    code += shift(high, 0)
    table.foreach(_ => (0 until 4).foreach(_ => code += UniqueByte(0)))
  }

  override def getOpcode: Option[BytecodeOpcode] = Option(BytecodeOpcode.TABLESWITCH)

  override def toString: String = s"TableSwitchInstruction($default, $low, $high, $table)"
}
