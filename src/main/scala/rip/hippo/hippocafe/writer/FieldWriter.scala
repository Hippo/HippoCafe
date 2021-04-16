package rip.hippo.hippocafe.writer

import rip.hippo.hippocafe.FieldInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/16/21
 * @since 1.1.0
 */
trait FieldWriter {
  def write(fieldInfo: FieldInfo, constantPool: ConstantPool, attributeWriter: AttributeWriter, out: DataOutputStream)
}