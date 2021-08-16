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

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.ClassReader
import rip.hippo.hippocafe.access.AccessFlag

import scala.util.{Failure, Success, Using}

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.1.0
 */
final class InstructionReadSuite extends AnyFunSuite {

  private val className = "AnnotationTest"

  test("CodeDisassembler.disassemble") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val test = Using(new ClassReader(value)) {
          classReader =>
            val classFile = classReader.classFile

            classFile.constantPool.get.info.foreach(println)

            classFile.methods.foreach(info => {

              info.attributes.foreach(println)
              info.instructions.foreach(println(_))
              info.tryCatchBlocks.foreach(println(_))
              info.attributes.foreach(println)
              println(info.name)
              println()
            })
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
