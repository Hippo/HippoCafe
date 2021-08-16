package rip.hippo.hippocafe.io.writer.impl

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.io.writer.AttributeWriter

import java.io.{ByteArrayOutputStream, DataOutputStream}
import scala.util.Using

/**
 * @author Hippo
 * @version 1.0.0, 4/18/21
 * @since 1.4.0
 */
final case class StandardAttributeWriter() extends AttributeWriter {
  override def write(attributeInfo: AttributeInfo, constantPool: ConstantPool, out: DataOutputStream): Unit = {
    Using(new ByteArrayOutputStream()) {
      byteArrayOutputStream =>
        Using(new DataOutputStream(byteArrayOutputStream)) {
          dataOutputStream =>
            attributeInfo.write(dataOutputStream, constantPool)
            val attributeData = byteArrayOutputStream.toByteArray

            out.writeShort(constantPool.findUTF8(attributeInfo.kind.toString))
            out.writeInt(attributeData.length)
            out.write(attributeData)
        }
    }
  }
}
