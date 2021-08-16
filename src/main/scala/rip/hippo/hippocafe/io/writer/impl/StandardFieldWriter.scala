package rip.hippo.hippocafe.io.writer.impl

import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.{ClassFile, FieldInfo}
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.writer.{AttributeWriter, ClassAttributesMetaWriter}

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardFieldWriter() extends ClassAttributesMetaWriter {
  override def write(classFile: ClassFile, constantPool: ConstantPool, attributeWriter: AttributeWriter, out: DataOutputStream): Unit = {
    out.writeShort(classFile.fields.size)
    classFile.fields.foreach(field => {
      out.writeShort(AccessFlag.toMask(field.accessFlags: _*))
      out.writeShort(constantPool.findUTF8(field.name))
      out.writeShort(constantPool.findUTF8(field.descriptor))
      out.writeShort(field.attributes.size)
      field.attributes.foreach(attributeWriter.write(_, constantPool, out))
    })
  }
}
