package rip.hippo.hippocafe.disassembler.instruction.constant.impl

import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{FloatInfo, StringInfo}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
final case class FloatConstant(value: Float) extends Constant[Float] {
  override val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo] = classOf[FloatInfo]
}
