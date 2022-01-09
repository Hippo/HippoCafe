package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{MethodHandleInfo, NameAndTypeInfo, ReferenceInfo, StringInfo}
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind._
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class MethodHandleConstant(var referenceKind: ReferenceKind,
                                      var owner: String,
                                      var name: String,
                                      var descriptor: String,
                                      var isInterface: Boolean) extends Constant[Int] {
  
  override val constantPoolInfoAssociate: Class[? <: ConstantPoolInfo] = classOf[MethodHandleInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit =
    constantPool.insertIfAbsent(makeInfo)

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.findInfo(makeInfo)


  def makeInfo: MethodHandleInfo =
    new MethodHandleInfo(referenceKind, new ReferenceInfo(new StringInfo(owner, ConstantPoolKind.CLASS), new NameAndTypeInfo(name, descriptor), getConstantPoolKind))

  def getConstantPoolKind: ConstantPoolKind =
    referenceKind match {
      case REF_GET_FIELD | REF_GET_STATIC | REF_PUT_FIELD | REF_PUT_STATIC => ConstantPoolKind.FIELD_REF
      case REF_INVOKE_VIRTUAL | REF_INVOKE_SPECIAL => ConstantPoolKind.METHOD_REF
      case REF_INVOKE_INTERFACE => ConstantPoolKind.INTERFACE_METHOD_REF
      case _ if isInterface => ConstantPoolKind.INTERFACE_METHOD_REF
      case _ => ConstantPoolKind.METHOD_REF
    }

  override def getValue: Int = -1
}
