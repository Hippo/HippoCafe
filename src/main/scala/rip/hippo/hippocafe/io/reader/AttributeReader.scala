package rip.hippo.hippocafe.io.reader

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
trait AttributeReader:
  def read(parentStream: DataInputStream, inputStream: DataInputStream, constantPool: ConstantPool, oak: Boolean): AttributeInfo