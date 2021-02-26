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

package rip.hippo.hippocafe.attribute.impl

import java.io.DataOutputStream
import rip.hippo.hippocafe.attribute.Attribute.Attribute
import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.attribute.impl.data.module.ExportsModuleData
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.attribute.impl.data.module.{ExportsModuleData, OpensModuleData, ProvidesModuleData, RequiresModuleData}
import rip.hippo.hippocafe.constantpool.ConstantPool

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
final case class ModuleAttribute(moduleNameIndex: Int,
                                 moduleFlags: Int,
                                 moduleVersionIndex: Int,
                                 requiresCount: Int,
                                 requires: Array[RequiresModuleData],
                                 exportsCount: Int,
                                 exports: Array[ExportsModuleData],
                                 opensCount: Int,
                                 opens: Array[OpensModuleData],
                                 usesCount: Int,
                                 usesIndex: Array[Int],
                                 providesCount: Int,
                                 provides: Array[ProvidesModuleData]) extends AttributeInfo {

  override val kind: Attribute = Attribute.MODULE


  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(moduleNameIndex)
    out.writeShort(moduleFlags)
    out.writeShort(moduleVersionIndex)

    out.writeShort(requiresCount)
    requires.foreach(require => {
      out.writeShort(require.requiresIndex)
      out.writeShort(require.requiresFlags)
      out.writeShort(require.requiresVersionIndex)
    })

    out.writeShort(exportsCount)
    exports.foreach(export => {
      out.writeShort(export.exportsIndex)
      out.writeShort(export.exportsFlags)
      out.writeShort(export.exportsToCount)
      export.exportsToIndex.foreach(index => out.writeShort(index))
    })

    out.writeShort(opensCount)
    opens.foreach(open => {
      out.writeShort(open.opensIndex)
      out.writeShort(open.opensFlags)
      out.writeShort(open.opensToCount)
      open.opensToIndex.foreach(index => out.writeShort(index))
    })

    out.writeShort(usesCount)
    usesIndex.foreach(index => out.writeShort(index))

    out.writeShort(providesCount)
    provides.foreach(provide => {
      out.writeShort(provide.providesIndex)
      out.writeShort(provide.providesWithCount)
      provide.providesWithIndex.foreach(index => out.writeShort(index))
    })
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit = {

  }
}
