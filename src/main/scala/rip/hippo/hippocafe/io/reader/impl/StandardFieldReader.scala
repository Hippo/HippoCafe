package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.FieldInfo
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.reader.{AttributeReader, FieldReader}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardFieldReader() extends FieldReader {
  override def read(inputStream: DataInputStream, attributeReader: AttributeReader, constantPool: ConstantPool, oak: Boolean): Array[FieldInfo] = {
    val array = new Array[FieldInfo](inputStream.readUnsignedShort())

    (0 until array.length).foreach(i => {
      val accessMask = inputStream.readUnsignedShort()
      val name = constantPool.readUTF8(inputStream.readUnsignedShort())
      val descriptor = constantPool.readUTF8(inputStream.readUnsignedShort())
      val attributesCount = inputStream.readUnsignedShort()
      val fieldInfo = new FieldInfo(name, descriptor, accessMask)
      (0 until attributesCount).foreach(_ => fieldInfo.attributes += attributeReader.read(inputStream, inputStream, constantPool, oak))
      array(i) = fieldInfo
    })

    array
  }
}
