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
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class LookupSwitchInstruction(var default: LabelInstruction) extends Instruction {
  val pairs: mutable.Map[Int, LabelInstruction] = mutable.Map[Int, LabelInstruction]()

  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    val code = assemblerContext.code
    val uniqueByte = UniqueByte(BytecodeOpcode.LOOKUPSWITCH.opcode.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    assemblerContext.preprocessedLookupSwitch += (this -> uniqueByte)
    code += uniqueByte
    (0 until 4).foreach(_ => code += UniqueByte(0))
    assemblerContext.switchPadding += (code(code.indexOf(uniqueByte) + 1) -> (-code.length & 3))
    val pairCount = pairs.size
    code += UniqueByte(((pairCount >>> 24) & 0xFF).toByte)
    code += UniqueByte(((pairCount >>> 16) & 0xFF).toByte)
    code += UniqueByte(((pairCount >>> 8) & 0xFF).toByte)
    code += UniqueByte((pairCount & 0xFF).toByte)
    pairs.keys.foreach(key => {
      code += UniqueByte(((key >>> 24) & 0xFF).toByte)
      code += UniqueByte(((key >>> 16) & 0xFF).toByte)
      code += UniqueByte(((key >>> 8) & 0xFF).toByte)
      code += UniqueByte((key & 0xFF).toByte)
      (0 until 4).foreach(_ => code += UniqueByte(0))
    })
  }

  def getLabels: ListBuffer[LabelInstruction] = {
    val buffer = ListBuffer[LabelInstruction]()
    buffer += default
    buffer ++= pairs.values
    buffer
  }
  
  override def getOpcode: Option[BytecodeOpcode] = Option(BytecodeOpcode.LOOKUPSWITCH)

  override def toString: String = s"LookupSwitchInstruction($default, $pairs)"
}
