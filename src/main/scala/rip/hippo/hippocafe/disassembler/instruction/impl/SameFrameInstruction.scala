package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.stackmap.impl.{SameExtendedStackMapFrame, SameStackMapFrame}

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final class SameFrameInstruction extends Instruction with FrameInstruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.labelQueue += this
  }

  override def assemble(assemblerContext: AssemblerContext): Unit = {
    val nextDelta = assemblerContext.nextDelta(this)

    if (nextDelta < 64) {
      assemblerContext.stackMapFrames += SameStackMapFrame(nextDelta)
    } else {
      assemblerContext.stackMapFrames += SameExtendedStackMapFrame(nextDelta)
    }
  }

  override def toString: String = "SameFrameInstruction()"
}

object SameFrameInstruction {
  def apply(): SameFrameInstruction = new SameFrameInstruction()
  def unapply[T <: Instruction](instruction: T): Boolean = instruction.isInstanceOf[SameFrameInstruction]
}

