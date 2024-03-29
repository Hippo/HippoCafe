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

package rip.hippo.testing.hippocafe.assemble

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.constantpool.info.impl.DynamicInfo
import rip.hippo.hippocafe.disassembler.instruction.FrameInstruction
import rip.hippo.hippocafe.disassembler.instruction.impl.{ANewArrayInstruction, AppendFrameInstruction, BranchInstruction, ChopFrameInstruction, ConstantInstruction, FullFrameInstruction, IncrementInstruction, InvokeDynamicInstruction, LabelInstruction, LineNumberInstruction, LookupSwitchInstruction, MultiANewArrayInstruction, NewArrayInstruction, PushInstruction, ReferenceInstruction, SameFrameInstruction, SameLocalsFrameInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.hippocafe.version.MajorClassFileVersion
import rip.hippo.hippocafe.{ClassReader, ClassWriter}
import rip.hippo.testing.hippocafe.CustomClassLoader

import java.io.FileOutputStream
import scala.collection.mutable.ListBuffer
import scala.util.{Failure, Using}

/**
 * @author Hippo
 * @version 1.0.0, 2/25/21
 * @since 1.1.0
 */
final class ReadAssembleWriteSuite extends AnyFunSuite {

  private val className = "ParameterFrameTest"


  test("assemble.readThenLoad") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val test = Using(new ClassReader(value)) {
          classReader =>

            //classReader.classFile.constantPool.foreach(_.info.foreach(println))

            println("-- FULL INSTRUCTIONS START --")
            classReader.classFile.methods.foreach(m => {
              println(m.name)
              m.instructions.foreach(println)
              println("\n\n")
            })
            println("-- FULL INSTRUCTIONS END --")

            /*classReader.classFile.methods.foreach(info => {
              info.instructions.filter(_.isInstanceOf[FrameInstruction]).foreach(frame => {
                println(s"${info.name} -> $frame")
              })
            })*/


            val writerTest = Using(new ClassWriter(classReader.classFile).calculateMaxes.generateFrames) {
              classWriter =>
                val bytecode = classWriter.write

                val loaded = new CustomClassLoader().createClass(className, bytecode)

                loaded.getDeclaredMethod("main", classOf[Array[String]]).invoke(null, null)
            }
            writerTest match {
              case Failure(exception) => throw exception
              case _  =>
            }
        }
        test match {
          case Failure(exception) => throw exception
          case _ =>
        }
        value.close()
      case None => println(s"Could not load resource $className.class")
    }
  }

}
