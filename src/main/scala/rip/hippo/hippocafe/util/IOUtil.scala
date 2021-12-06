package rip.hippo.hippocafe.util

import java.io.{ByteArrayOutputStream, InputStream}
import java.util
import scala.annotation.tailrec

/**
 * @author Hippo
 * @version 1.0.0, 12/6/21
 * @since 2.0.0
 */
object IOUtil {

  def toByteArray(inputStream: InputStream, initSize: Int = 8192): Array[Byte] = {
    var buf = new Array[Byte](initSize)
    val step = initSize
    var pos, n = 0
    while ({
      if (pos + step > buf.length) buf = util.Arrays.copyOf(buf, buf.length << 1)
      n = inputStream.read(buf, pos, step)
      n != -1
    }) pos += n
    if (pos != buf.length) buf = util.Arrays.copyOf(buf, pos)
    buf
  }

}
