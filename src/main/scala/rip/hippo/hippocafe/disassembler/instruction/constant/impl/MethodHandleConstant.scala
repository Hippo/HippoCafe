package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{MethodHandleInfo, NameAndTypeInfo, ReferenceInfo, StringInfo}
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class MethodHandleConstant(referenceKind: ReferenceKind, owner: String, name: String, descriptor: String) extends Constant[Int] {

  override val value: Int = -1

  override val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo] = classOf[MethodHandleInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit =
    constantPool.insertIfAbsent(makeInfo)

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.findInfo(makeInfo)


  def makeInfo: MethodHandleInfo =
    new MethodHandleInfo(referenceKind, new ReferenceInfo(new StringInfo(owner, ConstantPoolKind.CLASS), new NameAndTypeInfo(name, descriptor), ConstantPoolKind.METHOD_REF))
}
