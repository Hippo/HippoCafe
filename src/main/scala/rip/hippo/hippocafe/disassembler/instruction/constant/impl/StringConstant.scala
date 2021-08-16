package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.StringInfo
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
final case class StringConstant(value: String) extends Constant[String] {
  override val constantPoolInfoAssociate: Class[? <: ConstantPoolInfo] = classOf[StringInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit =
    constantPool.insertStringIfAbsent(value, ConstantPoolKind.STRING)

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.findString(value, ConstantPoolKind.STRING)
}
