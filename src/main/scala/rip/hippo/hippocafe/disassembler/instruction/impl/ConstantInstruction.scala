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
import rip.hippo.hippocafe.constantpool.info.impl.DoubleInfo
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.ValueAwareness
import rip.hippo.hippocafe.constantpool.info.impl.{DoubleInfo, FloatInfo, IntegerInfo, LongInfo, StringInfo, UTF8Info}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
final case class ConstantInstruction(constant: Any) extends Instruction {
  override def assemble(code: ListBuffer[Byte], constantPool: ConstantPool): Unit = {
    var index = -1
    // find if the constant is already present
    constant match {
      case _: String =>
        constantPool.info
          .filter(_._2.isInstanceOf[StringInfo])
          .filter(_._2.asInstanceOf[StringInfo].value.equals(constant))
          .keys
          .foreach(index = _)
      case _ =>
        constantPool.info
          .filter(_._2.isInstanceOf[ValueAwareness[_]])
          .filter(_._2.asInstanceOf[ValueAwareness[_]].value.equals(constant))
          .keys
          .foreach(index = _)
    }
    // if not make it
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      constant match { // TODO: finish
        case string: String =>
          constantPool.insert(index, UTF8Info(string))
          constantPool.insert(index, new StringInfo(string, ConstantPoolKind.STRING))
        case int: Int =>
          constantPool.insert(index, IntegerInfo(int))
        case float: Float =>
          constantPool.insert(index, FloatInfo(float))
        case double: Double =>
          constantPool.insert(index, DoubleInfo(double))
        case long: Long =>
          constantPool.insert(index, LongInfo(long))
      }
    }


    constant match {
      case _: Double | Long =>
        code += BytecodeOpcode.LDC2_W.id.asInstanceOf[Byte]
        code += (index << 8).asInstanceOf[Byte]
        code += (index & 0xFF).asInstanceOf[Byte]
      case _ =>
        if (index > 255) {
          code += BytecodeOpcode.LDC_W.id.asInstanceOf[Byte]
          code += (index << 8).asInstanceOf[Byte]
          code += (index & 0xFF).asInstanceOf[Byte]
        } else {
          code += BytecodeOpcode.LDC.id.asInstanceOf[Byte]
          code += (index).asInstanceOf[Byte]
        }
    }
  }
}

