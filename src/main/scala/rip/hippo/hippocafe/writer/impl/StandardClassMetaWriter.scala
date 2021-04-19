package rip.hippo.hippocafe.writer.impl

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.writer.ClassMetaWriter

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardClassMetaWriter() extends ClassMetaWriter {
  override def write(classFile: ClassFile, constantPool: ConstantPool, out: DataOutputStream): Unit = {
    out.writeShort(AccessFlag.toMask(classFile.access: _*))
    out.writeShort(constantPool.findString(classFile.name))
    out.writeShort(constantPool.findString(classFile.superName))
  }
}
