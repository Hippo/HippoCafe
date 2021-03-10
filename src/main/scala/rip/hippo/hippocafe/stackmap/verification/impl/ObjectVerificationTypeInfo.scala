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

package rip.hippo.hippocafe.stackmap.verification.impl

import rip.hippo.hippocafe.constantpool.info.impl.{StringInfo, UTF8Info}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
final class ObjectVerificationTypeInfo(val name: String) extends VerificationTypeInfo {
  override val tag: Int = 7


  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    super.write(out, constantPool)
    out.writeShort(constantPool.info
      .filter(_._2.isInstanceOf[StringInfo])
      .filter(_._2.kind == ConstantPoolKind.CLASS)
      .filter(_._2.asInstanceOf[StringInfo].value.equals(name))
      .keys
      .head)
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit =
    constantPool.insertStringIfAbsent(name, ConstantPoolKind.CLASS)

  override def toString: String = s"ObjectVerificationTypeInfo($name)"
}
