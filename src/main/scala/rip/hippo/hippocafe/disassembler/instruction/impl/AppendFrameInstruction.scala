package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.stackmap.impl.AppendStackMapFrame
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final class AppendFrameInstruction(var locals: Array[VerificationTypeInfo]) extends Instruction with FrameInstruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.preprocessedFrames += (this -> assemblerContext.code.length)
  }

  override def assemble(assemblerContext: AssemblerContext): Unit = {
    val nextDelta = assemblerContext.nextDelta(this)

    assemblerContext.stackMapFrames += AppendStackMapFrame(locals.length + 251, nextDelta, locals)
  }
}

object AppendFrameInstruction {
  def apply(locals: Array[VerificationTypeInfo]): AppendFrameInstruction = new AppendFrameInstruction(locals)
  def unapply[T <: Instruction](instruction: T): Option[Array[VerificationTypeInfo]] =
    instruction match {
      case appendFrameInstruction: AppendFrameInstruction =>
        Some(appendFrameInstruction.locals)
      case _ => None
    }
}

