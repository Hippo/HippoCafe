package rip.hippo.hippocafe.attribute.impl

import rip.hippo.hippocafe.attribute.Attribute.Attribute
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.attribute.impl.data.RecordComponentInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class RecordAttribute(components: Seq[RecordComponentInfo]) extends AttributeInfo {
  override val kind: Attribute = Attribute.RECORD

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(components.size)
    components.foreach(component => {
      out.writeShort(constantPool.findUTF8(component.name))
      out.writeShort(constantPool.findUTF8(component.descriptor))
      out.writeShort(component.attributes.size)
      component.attributes.foreach(_.buildConstantPool(constantPool))
      component.attributes.foreach(_.write(out, constantPool))
    })
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit =
    components.foreach(_.buildConstantPool(constantPool))
}
