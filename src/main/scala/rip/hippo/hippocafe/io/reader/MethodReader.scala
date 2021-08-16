package rip.hippo.hippocafe.io.reader

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.constantpool.ConstantPool

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/16/21
 * @since 2.0.0
 */
trait MethodReader:
  def read(inputStream: DataInputStream, attributeReader: AttributeReader, constantPool: ConstantPool, oak: Boolean): Array[MethodInfo]
