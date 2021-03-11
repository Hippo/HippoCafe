package rip.hippo.hippocafe.attribute.impl.data.annotation.target

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 3/10/21
 * @since 1.1.0
 */
final case class AnnotationTargetTypeContext() {
  val data: ListBuffer[Byte] = ListBuffer[Byte]()

  def writeByte(value: Int): Unit =
    data += (value & 0xFF).toByte

  def writeShort(value: Int): Unit = {
    writeByte(value >>> 8)
    writeByte(value)
  }
}
