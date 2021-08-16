package rip.hippo.hippocafe.io.reader

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
trait InterfaceReader {
  def read(dataInputStream: DataInputStream): InterfaceData
}

sealed case class InterfaceData(interfaceCount: Int, interfaceIndexes: Array[Int])
