package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{LongInfo, StringInfo}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
final case class LongConstant(value: Long) extends Constant[Long] {
  override val constantPoolInfoAssociate: Class[? <: ConstantPoolInfo] = classOf[LongInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    var index = -1
    constantPool.info
      .filter(_._2.isInstanceOf[LongInfo])
      .filter(_._2.asInstanceOf[LongInfo].value == value)
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      constantPool.insert(index, LongInfo(value))
    }
  }

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.info
      .filter(_._2.isInstanceOf[LongInfo])
      .filter(_._2.asInstanceOf[LongInfo].value == value)
      .keys
      .head
}
