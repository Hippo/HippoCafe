package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.stackmap.impl.FullStackMapFrame
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final class FullFrameInstruction(var locals: Array[VerificationTypeInfo], var stack: Array[VerificationTypeInfo]) extends Instruction with FrameInstruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.preprocessedFrames += (this -> assemblerContext.code.length)
  }

  override def assemble(assemblerContext: AssemblerContext): Unit = {
    val nextDelta = assemblerContext.nextDelta(this)

    assemblerContext.stackMapFrames += FullStackMapFrame(nextDelta, locals.length, locals, stack.length, stack)
  }

  override def toString = "FullFrameInstruction([" + locals.mkString(", ") + "], [" + stack.mkString(", ") + "])"
}

object FullFrameInstruction {
  def apply(locals: Array[VerificationTypeInfo], stack: Array[VerificationTypeInfo]): FullFrameInstruction = new FullFrameInstruction(locals, stack)

  def unapply[T <: Instruction](instruction: T): Option[(Array[VerificationTypeInfo], Array[VerificationTypeInfo])] =
    instruction match {
      case fullFrameInstruction: FullFrameInstruction =>
        Some((fullFrameInstruction.locals, fullFrameInstruction.stack))
      case _ => None
    }
}

