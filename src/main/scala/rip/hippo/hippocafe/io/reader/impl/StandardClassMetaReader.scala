package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.io.reader.{ClassMetaData, ClassMetaReader}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardClassMetaReader() extends ClassMetaReader {
  override def read(inputStream: DataInputStream): ClassMetaData =
    ClassMetaData(inputStream.readUnsignedShort(), inputStream.readUnsignedShort(), inputStream.readUnsignedShort())
}
