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

package rip.hippo.testing.hippocafe


import org.scalatest.FunSuite
import rip.hippo.hippocafe.disassembler.instruction.impl.ReferenceInstruction
import rip.hippo.hippocafe.ClassWriter
import rip.hippo.hippocafe.version.MajorClassFileVersion._
import rip.hippo.hippocafe.access.AccessFlag._
import rip.hippo.hippocafe.builder.ClassBuilder
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode
import rip.hippo.hippocafe.disassembler.instruction.impl.{ConstantInstruction, ReferenceInstruction, SimpleInstruction}

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.1.0
 */
final class BuilderSuite extends FunSuite {

  test("ClassBuilder.result") {
    val classFile = ClassBuilder(
      SE8,
      "HelloWorld",
      "java/lang/Object",
      ACC_PUBLIC
    ).method(
      "<init>",
      "()V"
    ).apply(instructions => {
      instructions += SimpleInstruction(ALOAD_0)
      instructions += ReferenceInstruction(INVOKESPECIAL, "java/lang/Object", "<init>", "()V")
      instructions += SimpleInstruction(RETURN)
    }).method(
      "main",
      "([Ljava/lang/String)V",
      ACC_PUBLIC, ACC_STATIC
    ).apply(instructions => {
      instructions += ReferenceInstruction(GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintSteam;")
      instructions += ConstantInstruction("Hello World")
      instructions += ReferenceInstruction(INVOKEVIRTUAL, "java/io/PrintStream", "println", "(Ljava/lang/String;)V")
      instructions += SimpleInstruction(RETURN)
    }).result
    println(classFile.name)
    new ClassWriter(classFile).generateConstantPool.info.foreach(println)
  }

}
