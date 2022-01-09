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
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/2/20
 * @since 1.0.0
 */
final case class ModuleAttribute(var moduleName: String,
                                 var moduleFlags: Int,
                                 var moduleVersion: Option[String],
                                 requires: ListBuffer[RequiresModuleData],
                                 exports: ListBuffer[ExportsModuleData],
                                 opens: ListBuffer[OpensModuleData],
                                 uses: ListBuffer[String],
                                 provides: ListBuffer[ProvidesModuleData]) extends AttributeInfo {

  override val kind: Attribute = Attribute.MODULE


  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(constantPool.findString(moduleName, ConstantPoolKind.MODULE))
    out.writeShort(moduleFlags)
    moduleVersion match {
      case Some(value) =>
        out.writeShort(constantPool.findUTF8(value))
      case None =>
        out.writeShort(0)
    }

    out.writeShort(requires.size)
    requires.foreach(require => {
      out.writeShort(constantPool.findString(require.requires, ConstantPoolKind.MODULE))
      out.writeShort(require.requiresFlags)
      require.requiresVersion match {
        case Some(value) =>
          out.writeShort(constantPool.findUTF8(value))
        case None =>
          out.writeShort(0)
      }
    })

    out.writeShort(exports.size)
    exports.foreach(exportModule => {
      out.writeShort(constantPool.findString(exportModule.exports, ConstantPoolKind.PACKAGE))
      out.writeShort(exportModule.exportsFlags)
      out.writeShort(exportModule.exportsTo.size)
      exportModule.exportsTo.map(to => constantPool.findString(to, ConstantPoolKind.MODULE)).foreach(index => out.writeShort(index))
    })

    out.writeShort(opens.size)
    opens.foreach(open => {
      out.writeShort(constantPool.findString(open.opens, ConstantPoolKind.PACKAGE))
      out.writeShort(open.opensFlags)
      out.writeShort(open.opensTo.size)
      open.opensTo.map(to => constantPool.findString(to, ConstantPoolKind.MODULE)).foreach(index => out.writeShort(index))
    })

    out.writeShort(uses.size)
    uses.map(use => constantPool.findString(use, ConstantPoolKind.CLASS)).foreach(index => out.writeShort(index))

    out.writeShort(provides.size)
    provides.foreach(provide => {
      out.writeShort(constantPool.findString(provide.provides, ConstantPoolKind.CLASS))
      out.writeShort(provide.providesWith.size)
      provide.providesWith.map(provide => constantPool.findString(provide, ConstantPoolKind.STRING)).foreach(index => out.writeShort(index))
    })
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit = {
    constantPool.insertStringIfAbsent(moduleName, ConstantPoolKind.MODULE)
    moduleVersion match {
      case Some(value) =>
        constantPool.insertUTF8IfAbsent(value)
      case None =>
    }

    requires.foreach(data => {
      constantPool.insertStringIfAbsent(data.requires, ConstantPoolKind.MODULE)
      data.requiresVersion match {
        case Some(value) =>
          constantPool.insertUTF8IfAbsent(value)
        case None =>
      }
    })

    exports.foreach(data => {
      constantPool.insertStringIfAbsent(data.exports, ConstantPoolKind.PACKAGE)
      data.exportsTo.foreach(to => constantPool.insertStringIfAbsent(to, ConstantPoolKind.MODULE))
    })

    opens.foreach(open => {
      constantPool.insertStringIfAbsent(open.opens, ConstantPoolKind.PACKAGE)
      open.opensTo.foreach(to => constantPool.insertStringIfAbsent(to, ConstantPoolKind.MODULE))
    })

    uses.foreach(use => constantPool.insertStringIfAbsent(use, ConstantPoolKind.CLASS))
    
    provides.foreach(provide => {
      constantPool.insertStringIfAbsent(provide.provides, ConstantPoolKind.CLASS)
      provide.providesWith.foreach(provide => constantPool.insertStringIfAbsent(provide, ConstantPoolKind.CLASS))
    })
  }
}
