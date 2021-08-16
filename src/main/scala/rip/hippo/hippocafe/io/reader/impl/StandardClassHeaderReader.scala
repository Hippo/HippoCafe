package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.io.reader.{ClassHeaderReader, HeaderData}

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardClassHeaderReader() extends ClassHeaderReader {
  
  override def read(inputStream: DataInputStream): HeaderData =
    HeaderData(inputStream.readInt(), inputStream.readUnsignedShort(), inputStream.readUnsignedShort())
}
