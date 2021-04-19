package rip.hippo.hippocafe.writer.impl

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.writer.{AttributeWriter, ClassAttributedMetaWriter}

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardMethodWriter() extends ClassAttributedMetaWriter {
  override def write(classFile: ClassFile, constantPool: ConstantPool, attributeWriter: AttributeWriter, out: DataOutputStream): Unit = {
    out.writeShort(classFile.methods.size)
    classFile.methods.foreach(method => {
      out.writeShort(AccessFlag.toMask(method.accessFlags: _*))
      out.writeShort(constantPool.findUTF8(method.name))
      out.writeShort(constantPool.findUTF8(method.descriptor))
      out.writeShort(method.attributes.size)

      method.attributes.foreach(attributeWriter.write(_, constantPool, out))
    })
  }
}