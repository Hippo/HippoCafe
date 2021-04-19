package rip.hippo.hippocafe.writer

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/16/21
 * @since 1.4.0
 */
trait ClassAttributedMetaWriter {
  def write(classFile: ClassFile, constantPool: ConstantPool, attributeWriter: AttributeWriter, out: DataOutputStream)
}
