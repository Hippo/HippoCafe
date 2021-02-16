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

package rip.hippo.hippocafe

import rip.hippo.hippocafe.access.AccessFlag.AccessFlag
import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.version.MajorClassFileVersion.MajorClassFileVersion
import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.version.MajorClassFileVersion

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/3/20
 * @since 1.0.0
 */
final case class ClassFile(var majorClassFileVersion: MajorClassFileVersion, var name: String, var superName: String, var access: AccessFlag*) {

  val interfaces: ListBuffer[String] = ListBuffer[String]()
  val fields: ListBuffer[FieldInfo] = ListBuffer[FieldInfo]()
  val methods: ListBuffer[MethodInfo] = ListBuffer[MethodInfo]()
  val attributes: ListBuffer[AttributeInfo] = ListBuffer[AttributeInfo]()

  var minorVersion: Int = 0

  var constantPool: Option[ConstantPool] = None

  private[hippocafe] var lowLevel: Boolean = false

  def this(majorClassFileVersion: MajorClassFileVersion, name: String, superName: String, accessMask: Int) =
    this(majorClassFileVersion, name, superName, AccessFlag.fromMask(accessMask): _*)

  def isOak: Boolean = {
    val version = MajorClassFileVersion.JDK1_1.id
    majorClassFileVersion.id < version || (majorClassFileVersion.id == version && minorVersion < 3)
  }

}
