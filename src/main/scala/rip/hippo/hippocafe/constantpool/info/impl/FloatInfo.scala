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

package rip.hippo.hippocafe.constantpool.info.impl

import java.io.DataOutputStream
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.ConstantPoolKind
import rip.hippo.hippocafe.constantpool.info.ValueAwareness
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.{ConstantPoolInfo, ValueAwareness}

/**
 * @author Hippo
 * @version 1.0.1, 8/1/20
 * @since 1.0.0
 */
final case class FloatInfo(value: Float) extends ConstantPoolInfo with ValueAwareness[Float] {
  override val kind: ConstantPoolKind = ConstantPoolKind.FLOAT

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = out.writeFloat(value)

  override def readCallback(constantPool: ConstantPool): Unit = {}

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    var index = -1
    constantPool.info
      .filter(_._2.isInstanceOf[FloatInfo])
      .filter(_._2.asInstanceOf[FloatInfo].value == value)
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      constantPool.insert(index, this)
    }
  }
}
