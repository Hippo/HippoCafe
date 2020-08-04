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

package rip.hippo.api.hippocafe.constantpool.info.impl

import java.io.DataOutputStream

import rip.hippo.api.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.api.hippocafe.constantpool.ConstantPoolKind.ConstantPoolKind
import rip.hippo.api.hippocafe.constantpool.info.{ConstantPoolInfo, ValueAwareness}

/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
final class LongInfo(val value: Long) extends ConstantPoolInfo with ValueAwareness[Long] {
  override val kind: ConstantPoolKind = ConstantPoolKind.LONG
  override val wide: Boolean = true

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = out.writeLong(value)

  override def readCallback(constantPool: ConstantPool): Unit = {}
}
