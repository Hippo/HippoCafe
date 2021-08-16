package rip.hippo.hippocafe.io.writer.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.writer.ConstantPoolWriter

import java.io.DataOutputStream

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardConstantPoolWriter() extends ConstantPoolWriter {
  override def write(constantPool: ConstantPool, out: DataOutputStream): Unit = {
    out.writeShort(constantPool.info.keys.max + 1)
    constantPool.info.foreach(entry => {
      out.writeByte(entry._2.kind.tag)
      entry._2.insertIfAbsent(constantPool)
      entry._2.write(out, constantPool)
    })
  }
}
