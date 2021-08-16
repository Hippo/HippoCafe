package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.constantpool.ConstantPoolKind._
import rip.hippo.hippocafe.constantpool.info.impl.{DoubleInfo, DynamicInfo, FloatInfo, IntegerInfo, LongInfo, MethodHandleInfo, NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.io.reader.ConstantPoolReader

import java.io.DataInputStream

/**
 * @author Hippo
 * @version 1.0.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardConstantPoolReader() extends ConstantPoolReader {

  override def read(inputStream: DataInputStream, constantPool: ConstantPool): Unit = {

    def u1: Int = inputStream.readUnsignedByte()
    def u2: Int = inputStream.readUnsignedShort()
    def u4: Int = inputStream.readInt()


    val constantPoolSize = u2

    var i = 1
    while (i < constantPoolSize) {
      val tag = u1
      val constantPoolKind = ConstantPoolKind.fromTag(tag)

      constantPoolKind match {
        case Some(value) =>
          value match {
            case UTF8 => constantPool.insert(i, UTF8Info(inputStream.readUTF()))
            case CLASS |
                 STRING |
                 METHOD_TYPE |
                 MODULE |
                 PACKAGE => constantPool.insert(i, StringInfo(u2, value))
            case LONG =>
              constantPool.insert(i, LongInfo(inputStream.readLong()))
              i += 1
            case DOUBLE =>
              constantPool.insert(i, DoubleInfo(inputStream.readDouble()))
              i += 1
            case INVOKE_DYNAMIC | DYNAMIC => constantPool.insert(i, DynamicInfo(value, u2, u2))
            case INTEGER => constantPool.insert(i, IntegerInfo(u4))
            case FLOAT => constantPool.insert(i, FloatInfo(inputStream.readFloat()))
            case FIELD_REF |
                 METHOD_REF |
                 INTERFACE_METHOD_REF => constantPool.insert(i, ReferenceInfo(u2, u2, value))
            case NAME_AND_TYPE => constantPool.insert(i, NameAndTypeInfo(u2, u2))
            case METHOD_HANDLE => constantPool.insert(i, MethodHandleInfo(ReferenceKind.valueOf(u1), u2))
          }
        case None => throw new HippoCafeException(s"Could not find constant pool kind for tag $tag")
      }
      i += 1
    }

    constantPool.info.values.foreach(info => info.readCallback(constantPool))

  }
}
