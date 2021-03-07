package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.stackmap.impl.{SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final class SameLocalsFrameInstruction(var verificationTypeInfo: VerificationTypeInfo) extends Instruction with FrameInstruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.preprocessedFrames += (this -> assemblerContext.code.length)
  }

  override def assemble(assemblerContext: AssemblerContext): Unit = {
    val nextDelta = assemblerContext.nextDelta(this)

    if (nextDelta + 64 >= 64 && nextDelta + 64 <= 127) {
      assemblerContext.stackMapFrames += SameLocalsStackMapFrame(nextDelta + 64, Array[VerificationTypeInfo](verificationTypeInfo))
    } else {
      assemblerContext.stackMapFrames += SameLocalsExtendedStackMapFrame(nextDelta, Array[VerificationTypeInfo](verificationTypeInfo))
    }
  }

  override def toString: String = s"SameLocalsFrameInstruction($verificationTypeInfo)"
}

object SameLocalsFrameInstruction {
  def apply(verificationTypeInfo: VerificationTypeInfo): SameLocalsFrameInstruction = new SameLocalsFrameInstruction(verificationTypeInfo)
  def unapply[T <: Instruction](instruction: T): Option[VerificationTypeInfo] =
    instruction match {
      case sameLocalsFrameInstruction: SameLocalsFrameInstruction =>
        Some(sameLocalsFrameInstruction.verificationTypeInfo)
      case _ => None
    }
}

