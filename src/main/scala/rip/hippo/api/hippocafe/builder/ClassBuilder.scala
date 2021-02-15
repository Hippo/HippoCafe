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

package rip.hippo.api.hippocafe.builder

import rip.hippo.api.hippocafe.{ClassFile, FieldInfo, MethodInfo}
import rip.hippo.api.hippocafe.access.AccessFlag.AccessFlag
import rip.hippo.api.hippocafe.disassembler.instruction.Instruction
import rip.hippo.api.hippocafe.version.MajorClassFileVersion.MajorClassFileVersion

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/7/20
 * @since 1.0.0
 */
final class ClassBuilder(version: MajorClassFileVersion,
                         name: String,
                         superName: String,
                         access: AccessFlag*) {

  private val classFile = ClassFile(version, name, superName, access: _*)


  def field(name: String, descriptor: String, access: AccessFlag*): ClassBuilder = {
    classFile.fields += FieldInfo(name, descriptor, access: _*)
    this
  }

  def method(name: String, descriptor: String, access: AccessFlag*): MethodBuilder = new MethodBuilder(this, name, descriptor, access: _*)

  def result: ClassFile = classFile

  sealed class MethodBuilder(parent: ClassBuilder, name: String, descriptor: String, access: AccessFlag*) {
    private val methodInfo = MethodInfo(name, descriptor, access: _*)
    def apply(consumer: ListBuffer[Instruction] => Unit): ClassBuilder = {
      consumer(methodInfo.instructions)
      parent.classFile.methods += methodInfo
      parent
    }
  }

}

object ClassBuilder {
  def apply(version: MajorClassFileVersion,
            name: String,
            superName: String,
            access: AccessFlag*): ClassBuilder = new ClassBuilder(version, name, superName, access: _*)
}
