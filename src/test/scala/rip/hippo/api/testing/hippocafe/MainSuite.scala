/*
 * MIT License
 *
 * Copyright (c) 2020 Hippo
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

package rip.hippo.api.testing.hippocafe

import java.io.FileOutputStream

import org.scalatest.FunSuite
import rip.hippo.api.hippocafe.builder.ClassBuilder
import rip.hippo.api.hippocafe.translation.instruction.impl.{ConstantInstruction, ReferenceInstruction, SimpleInstruction}
import rip.hippo.api.hippocafe.{ClassReader, ClassWriter}

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.1.0
 */
final class MainSuite extends FunSuite {

  private val className = "SwitchTest"


  sealed class CustomClassLoader extends ClassLoader {
    def createClass(name: String, bytecode: Array[Byte]): Class[_] = super.defineClass(name, bytecode, 0, bytecode.length)
  }

  test("ClassReader.read") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val classReader = new ClassReader(value)
        val classFile = classReader.classFile
        println(classFile.name)
      case None => println(s"Could not load resource $className.class")
    }
  }

  test("CodeTranslator.translate") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val classReader = new ClassReader(value).translate
        val classFile = classReader.classFile
        classFile.methods.foreach(info => {
          println(info.name)
          info.instructions.foreach(println(_))
          info.tryCatchBlocks.foreach(println(_))
          println()
        })
      case None => println(s"Could not load resource $className.class")
    }
  }

  test("ClassBuilder.result") {
    import rip.hippo.api.hippocafe.version.MajorClassFileVersion._
    import rip.hippo.api.hippocafe.access.AccessFlag._
    import rip.hippo.api.hippocafe.translation.instruction.BytecodeOpcode._
    val classFile = ClassBuilder(
      SE8,
      "HelloWorld",
      "java/lang/Object",
      ACC_PUBLIC
    ).method(
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
  }


  test("ClassWriter.write") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val classReader = new ClassReader(value)
        val bytecode = new ClassWriter(classReader.classFile).write

        val loaded = new CustomClassLoader().createClass(className, bytecode)

        loaded.getDeclaredMethod("main", classOf[Array[String]]).invoke(null, null)
      case None => println(s"Could not load resource $className.class")
    }
  }

}
