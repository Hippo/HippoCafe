package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.io.reader.{InterfaceData, InterfaceReader}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardInterfaceReader() extends InterfaceReader {
  override def read(dataInputStream: DataInputStream): InterfaceData = {
    val count = dataInputStream.readUnsignedShort()
    val array = new Array[Int](count)
    (0 until count).foreach(i => array(i) = dataInputStream.readUnsignedShort())
    InterfaceData(count, array)
  }
}
