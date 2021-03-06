package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.attribute.impl.data.BootstrapMethodsAttributeData
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.constantpool.info.impl.{DynamicInfo, MethodHandleInfo, NameAndTypeInfo, ReferenceInfo, StringInfo}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class InvokeDynamicConstant(name: String,
                                       descriptor: String,
                                       bootstrapMethod: MethodHandleConstant,
                                       bootstrapArguments: ListBuffer[Constant[_]]) extends Constant[Int] {

  override val value: Int = -1

  override val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo] = classOf[MethodHandleInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    val info = makeInfo
    val ref = info.bootstrapMethodsAttributeData.bootstrapMethodRef

    /*ref.insertIfAbsent(constantPool)
    constantPool.insertIfAbsent(info.nameAndTypeInfo)
    constantPool.insertIfAbsent(ref)
    constantPool.insertIfAbsent(ref.referenceInfo)
    constantPool.insertIfAbsent(ref.referenceInfo.classInfo)
    constantPool.insertIfAbsent(ref.referenceInfo.nameAndTypeInfo)
    info.bootstrapMethodsAttributeData.bootstrapArguments.foreach(_.insertIfAbsent(constantPool))*/

    info.insertIfAbsent(constantPool)
  }

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.findInfo(makeInfo)


  def makeInfo: DynamicInfo =
    new DynamicInfo(ConstantPoolKind.INVOKE_DYNAMIC,
      BootstrapMethodsAttributeData(bootstrapMethod.makeInfo, bootstrapArguments),
      new NameAndTypeInfo(name, descriptor))
}
