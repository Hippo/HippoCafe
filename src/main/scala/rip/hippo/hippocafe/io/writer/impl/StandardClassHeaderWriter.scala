package rip.hippo.hippocafe.io.writer.impl

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.io.writer.ClassHeaderWriter

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardClassHeaderWriter() extends ClassHeaderWriter {
  override def write(classFile: ClassFile, out: DataOutputStream): Unit = {
    out.writeInt(0xCAFEBABE)

    out.writeShort(classFile.minorVersion)
    out.writeShort(classFile.majorClassFileVersion.id)
  }
}
