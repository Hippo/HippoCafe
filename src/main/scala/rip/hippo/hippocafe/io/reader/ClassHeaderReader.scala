package rip.hippo.hippocafe.io.reader

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
trait ClassHeaderReader {
  def read(inputStream: DataInputStream): HeaderData
}

sealed case class HeaderData(magic: Int, minorVersion: Int, majorVersion: Int)