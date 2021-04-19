package rip.hippo.hippocafe.writer

import rip.hippo.hippocafe.ClassFile

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/14/21
 * @since 1.4.0
 */
trait ClassHeaderWriter {
  def write(classFile: ClassFile, out: DataOutputStream): Unit
}
