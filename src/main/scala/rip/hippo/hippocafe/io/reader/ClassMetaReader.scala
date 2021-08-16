package rip.hippo.hippocafe.io.reader

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
trait ClassMetaReader {
  def read(inputStream: DataInputStream): ClassMetaData
}

sealed case class ClassMetaData(accessMask: Int, classNameIndex: Int, superNameIndex: Int)
