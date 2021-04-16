package rip.hippo.hippocafe.writer

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/14/21
 * @since 1.1.0
 */
trait ClassMetaWriter {
  def write(classFile: ClassFile, constantPool: ConstantPool, out: DataOutputStream)
}
