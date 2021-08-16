package rip.hippo.hippocafe.io.reader

import java.io.DataInputStream

import rip.hippo.hippocafe.constantpool.ConstantPool


/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
trait ConstantPoolReader:
  def read(inputStream: DataInputStream, constantPool: ConstantPool): Unit