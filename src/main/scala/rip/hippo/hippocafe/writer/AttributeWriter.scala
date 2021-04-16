package rip.hippo.hippocafe.writer

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/16/21
 * @since 1.1.0
 */
trait AttributeWriter {
  def write(attributeInfo: AttributeInfo, constantPool: ConstantPool, out: DataOutputStream)
}
