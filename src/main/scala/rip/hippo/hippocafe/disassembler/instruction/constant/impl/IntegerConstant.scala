package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{IntegerInfo, StringInfo}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
final case class IntegerConstant(value: Int) extends Constant[Int] {
  override val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo] = classOf[IntegerInfo]

  override def insertIfAbsent(constantPool: ConstantPool): Unit = {
    var index = -1
    constantPool.info
      .filter(_._2.isInstanceOf[IntegerInfo])
      .filter(_._2.asInstanceOf[IntegerInfo].value == value)
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = constantPool.info.keys.max
      index = max + (if (constantPool.info(max).wide) 2 else 1)
      constantPool.insert(index, IntegerInfo(value))
    }
  }

  override def getConstantPoolIndex(constantPool: ConstantPool): Int =
    constantPool.info
      .filter(_._2.isInstanceOf[IntegerInfo])
      .filter(_._2.asInstanceOf[IntegerInfo].value == value)
      .keys
      .head
}
