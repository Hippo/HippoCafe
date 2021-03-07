package rip.hippo.hippocafe.disassembler.instruction.constant

import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
trait Constant[T] {
  val constantPoolInfoAssociate: Class[_ <: ConstantPoolInfo]
  val value: T
}
