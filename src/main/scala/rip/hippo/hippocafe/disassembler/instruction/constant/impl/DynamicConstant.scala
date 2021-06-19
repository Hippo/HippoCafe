package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.attribute.impl.data.BootstrapMethodsAttributeData
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{DynamicInfo, MethodHandleInfo, NameAndTypeInfo}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class DynamicConstant(name: String,
                                 descriptor: String,
                                 bootstrapMethod: MethodHandleConstant,
                                 bootstrapArguments: ListBuffer[Constant[_]]) extends Constant[Int] {

  override val value: Int = -1

  override val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo] = classOf[MethodHandleInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    val info = makeInfo
    constantPool.insertIfAbsent(info.nameAndTypeInfo)
    constantPool.insertIfAbsent(info.bootstrapMethodsAttributeData.bootstrapMethodRef)
    info.bootstrapMethodsAttributeData.bootstrapArguments.foreach(_.insertIfAbsent(constantPool))
    constantPool.insertIfAbsent(info)
  }

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.findInfo(makeInfo)


  def makeInfo: DynamicInfo =
    new DynamicInfo(ConstantPoolKind.DYNAMIC,
      BootstrapMethodsAttributeData(bootstrapMethod.makeInfo, bootstrapArguments),
      new NameAndTypeInfo(name, descriptor))
}
