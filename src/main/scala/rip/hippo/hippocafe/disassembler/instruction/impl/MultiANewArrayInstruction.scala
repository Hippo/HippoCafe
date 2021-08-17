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

import rip.hippo.hippocafe.constantpool.info.impl.{StringInfo, UTF8Info}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}
import rip.hippo.hippocafe.util.Type

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class MultiANewArrayInstruction(var descriptor: String, var dimensions: Int) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    var index = -1
    
    constantPool.info
      .filter(_._2.isInstanceOf[StringInfo])
      .filter(_._2.kind == ConstantPoolKind.CLASS)
      .filter(_._2.asInstanceOf[StringInfo].value.equals(descriptor))
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      if (!constantPool.info.values.exists(info => info.isInstanceOf[UTF8Info] && info.asInstanceOf[UTF8Info].value.equals(descriptor))) {
        constantPool.insert(index, UTF8Info(descriptor))
        index += 1
      }
      constantPool.insert(index, new StringInfo(descriptor, ConstantPoolKind.CLASS))
    }



    val uniqueByte = UniqueByte(BytecodeOpcode.MULTIANEWARRAY.opcode.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    assemblerContext.code += uniqueByte
    assemblerContext.code += UniqueByte(((index >>> 8) & 0xFF).toByte)
    assemblerContext.code += UniqueByte((index & 0xFF).toByte)
    assemblerContext.code += UniqueByte(dimensions.toByte)
  }

  override def getOpcode: Option[BytecodeOpcode] = Option(BytecodeOpcode.MULTIANEWARRAY)
}
