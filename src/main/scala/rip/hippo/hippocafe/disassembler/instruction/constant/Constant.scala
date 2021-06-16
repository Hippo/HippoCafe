package rip.hippo.hippocafe.disassembler.instruction.constant

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.ConstantPoolKind.{CLASS, DOUBLE, DYNAMIC, FIELD_REF, FLOAT, INTEGER, INTERFACE_METHOD_REF, INVOKE_DYNAMIC, LONG, METHOD_HANDLE, METHOD_REF, METHOD_TYPE, MODULE, NAME_AND_TYPE, PACKAGE, STRING, UTF8}
import rip.hippo.hippocafe.constantpool.info.impl.StringInfo
import rip.hippo.hippocafe.constantpool.info.{ConstantPoolInfo, ValueAwareness}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.{ClassConstant, DoubleConstant, FloatConstant, IntegerConstant, LongConstant, StringConstant, UTF8Constant}
import rip.hippo.hippocafe.exception.HippoCafeException

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
trait Constant[T] {
  val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo]
  val value: T

  def insertIfAbsent(constantPool: ConstantPool): Unit
  def getConstantPoolIndex(constantPool: ConstantPool): Int
}


object Constant {
  def fromInfo(constantPoolInfo: ConstantPoolInfo): Constant[_] =
    constantPoolInfo match { // TODO: finish
      case aware: ValueAwareness[_] =>
        aware.value match {
        case int: Int => IntegerConstant(int)
        case float: Float => FloatConstant(float)
        case long: Long => LongConstant(long)
        case double: Double => DoubleConstant(double)
        case double: java.lang.Double => DoubleConstant(double)
        case long: java.lang.Long => LongConstant(long)
        case int: java.lang.Integer => IntegerConstant(int)
        case float: java.lang.Float => FloatConstant(float)
        case utf8: String => UTF8Constant(utf8)
        case x => throw new HippoCafeException(s"Invalid constant instruction $x")
      }
      case string: StringInfo =>
        string.kind match {
          case CLASS => ClassConstant(string.value)
          case STRING => StringConstant(string.value)
          case x => throw new HippoCafeException(s"Invalid constant instruction $x")
        }
      case x => throw new HippoCafeException(s"Invalid constant instruction $x")
    }
}