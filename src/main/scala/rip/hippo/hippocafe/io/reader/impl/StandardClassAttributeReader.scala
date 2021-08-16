package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.reader.{AttributeReader, ClassAttributesReader}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/16/21
 * @since 2.0.0
 */
final case class StandardClassAttributeReader() extends ClassAttributesReader {
  override def read(inputStream: DataInputStream, attributeReader: AttributeReader, constantPool: ConstantPool, oak: Boolean): Array[AttributeInfo] = {
    val attributeCount = inputStream.readUnsignedShort()
    val array = new Array[AttributeInfo](attributeCount)

    (0 until attributeCount).foreach(i => array(i) = attributeReader.read(inputStream, inputStream, constantPool, oak))

    array
  }
}
