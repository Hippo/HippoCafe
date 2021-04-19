package rip.hippo.hippocafe.writer.impl

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.writer.ClassMetaWriter

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardInterfaceWriter() extends ClassMetaWriter {
  override def write(classFile: ClassFile, constantPool: ConstantPool, out: DataOutputStream): Unit = {
    out.writeShort(classFile.interfaces.size)
    classFile.interfaces.foreach(interface => out.writeShort(constantPool.findString(interface)))
  }
}
