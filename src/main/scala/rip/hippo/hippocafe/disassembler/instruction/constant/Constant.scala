package rip.hippo.hippocafe.disassembler.instruction.constant

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.ConstantPoolKind.{CLASS, DOUBLE, DYNAMIC, FIELD_REF, FLOAT, INTEGER, INTERFACE_METHOD_REF, INVOKE_DYNAMIC, LONG, METHOD_HANDLE, METHOD_REF, METHOD_TYPE, MODULE, NAME_AND_TYPE, PACKAGE, STRING, UTF8}
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.constantpool.info.impl.{DynamicInfo, MethodHandleInfo, StringInfo}
import rip.hippo.hippocafe.constantpool.info.{ConstantPoolInfo, ValueAwareness}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.{ClassConstant, DoubleConstant, DynamicConstant, FloatConstant, IntegerConstant, InvokeDynamicConstant, LongConstant, MethodHandleConstant, MethodTypeConstant, ModuleConstant, PackageConstant, StringConstant, UTF8Constant}
import rip.hippo.hippocafe.exception.HippoCafeException

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
trait Constant[T] {
  val constantPoolInfoAssociate: Class[? <: ConstantPoolInfo]
  val value: T

  def insertIfAbsent(constantPool: ConstantPool): Unit
  def getConstantPoolIndex(constantPool: ConstantPool): Int
}


object Constant {
  def fromInfo(constantPoolInfo: ConstantPoolInfo): Constant[?] =
    constantPoolInfo match {
      case aware: ValueAwareness[?] =>
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
          case METHOD_TYPE => MethodTypeConstant(string.value)
          case MODULE => ModuleConstant(string.value)
          case PACKAGE => PackageConstant(string.value)
          case x => throw new HippoCafeException(s"Invalid constant instruction $x")
        }
      case methodHandleInfo: MethodHandleInfo =>
        val referenceInfo = methodHandleInfo.referenceInfo
        MethodHandleConstant(methodHandleInfo.referenceKind,
          referenceInfo.classInfo.value,
          referenceInfo.nameAndTypeInfo.name,
          referenceInfo.nameAndTypeInfo.descriptor,
          methodHandleInfo.referenceKind == ReferenceKind.REF_INVOKE_INTERFACE)

      case dynamicInfo: DynamicInfo =>
        val nameAndType = dynamicInfo.nameAndTypeInfo
        val bsmData = dynamicInfo.bootstrapMethodsAttributeData
        dynamicInfo.kind match {
          case INVOKE_DYNAMIC => InvokeDynamicConstant(nameAndType.name,
            nameAndType.descriptor,
            Constant.fromInfo(bsmData.bootstrapMethodRef).asInstanceOf[MethodHandleConstant],
            bsmData.bootstrapArguments)
          case DYNAMIC => DynamicConstant(nameAndType.name,
            nameAndType.descriptor,
            Constant.fromInfo(bsmData.bootstrapMethodRef).asInstanceOf[MethodHandleConstant],
            bsmData.bootstrapArguments)

          case x => throw new HippoCafeException(s"Invalid constant instruction $x")
        }
      case x => throw new HippoCafeException(s"Invalid constant instruction $x")
    }
}