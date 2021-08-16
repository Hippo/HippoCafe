package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.reader.{AttributeReader, MethodReader}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/16/21
 * @since 2.0.0
 */
final case class StandardMethodReader() extends MethodReader {
  override def read(inputStream: DataInputStream, attributeReader: AttributeReader, constantPool: ConstantPool, oak: Boolean): Array[MethodInfo] = {
    val array = new Array[MethodInfo](inputStream.readUnsignedShort())

    (0 until array.length).foreach(i => {
      val accessMask = inputStream.readUnsignedShort()
      val name = constantPool.readUTF8(inputStream.readUnsignedShort())
      val descriptor = constantPool.readUTF8(inputStream.readUnsignedShort())
      val attributesCount = inputStream.readUnsignedShort()
      val MethodInfo = new MethodInfo(name, descriptor, accessMask)
      (0 until attributesCount).foreach(_ => MethodInfo.attributes += attributeReader.read(inputStream, inputStream, constantPool, oak))
      array(i) = MethodInfo
    })
    
    array
  }
}
