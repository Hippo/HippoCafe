package rip.hippo.hippocafe.io.writer

import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/14/21
 * @since 1.4.0
 */
trait ConstantPoolWriter:
  def write(constantPool: ConstantPool, out: DataOutputStream): Unit
