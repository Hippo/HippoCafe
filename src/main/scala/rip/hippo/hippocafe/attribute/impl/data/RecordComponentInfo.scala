package rip.hippo.hippocafe.attribute.impl.data

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class RecordComponentInfo(name: String, descriptor: String, attributes: Seq[AttributeInfo]) {
  def buildConstantPool(constantPool: ConstantPool): Unit = {
    constantPool.insertUTF8IfAbsent(name)
    constantPool.insertUTF8IfAbsent(descriptor)
  }
}
