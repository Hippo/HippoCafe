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

import java.io.{FileOutputStream, IOException, InputStream}

import rip.hippo.api.hippocafe.{ClassReader, ClassWriter}

/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
object Main {


  sealed class CustomClassLoader extends ClassLoader {
    def createClass(name: String, bytecode: Array[Byte]): Class[_] = super.defineClass(name, bytecode, 0, bytecode.length)
  }


  def main(args: Array[String]): Unit = {
    var inputStream: InputStream = null
    try {
      val className = "SwitchTest"
      inputStream = Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")
      val classReader = new ClassReader(inputStream)
      val bytecode = new ClassWriter(classReader.classFile).write

      val loaded = new CustomClassLoader().createClass(className, bytecode)

      loaded.getDeclaredMethod("main", classOf[Array[String]]).invoke(null, args)

    } catch {
      case e: Exception => e.printStackTrace()
    } finally if (inputStream != null) inputStream.close()
  }
}