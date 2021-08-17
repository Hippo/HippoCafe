package rip.hippo.hippocafe.disassembler.instruction

import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.UninitializedVerificationTypeInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/4/21
 * @since 1.0.0
 */
trait FrameInstruction:
  def assemble(assemblerContext: AssemblerContext): Unit
  def finalizeVerificationTypes(assemblerContext: AssemblerContext): Unit
  def alignUninitalized(assemblerContext: AssemblerContext, array: Array[VerificationTypeInfo]): Unit = {
    array.foreach {
      case info: UninitializedVerificationTypeInfo =>
        info.typeInstruction match {
          case Some(value) =>
            info.offset = assemblerContext.code.indexOf(assemblerContext.labelToByte(value).byte)
          case None =>
        }
      case _ =>
    }
  }
