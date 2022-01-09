package rip.hippo.hippocafe.attribute.impl.data

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class RecordComponentInfo(var name: String, var descriptor: String, attributes: ListBuffer[AttributeInfo]) {
  def buildConstantPool(constantPool: ConstantPool): Unit = {
    constantPool.insertUTF8IfAbsent(name)
    constantPool.insertUTF8IfAbsent(descriptor)
    attributes.foreach(_.buildConstantPool(constantPool))
  }
}
