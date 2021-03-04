package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.stackmap.impl.ChopStackMapFrame

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final class ChopFrameInstruction(var numberOfAbsentLocals: Int) extends Instruction with FrameInstruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.preprocessedFrames += (this -> assemblerContext.code.length)
  }

  override def assemble(assemblerContext: AssemblerContext): Unit = {
    val nextDelta = assemblerContext.nextDelta(this)

    assemblerContext.stackMapFrames += ChopStackMapFrame(251 - numberOfAbsentLocals, nextDelta)
  }
}

object ChopFrameInstruction {
  def apply(numberOfLocals: Int): ChopFrameInstruction = new ChopFrameInstruction(numberOfLocals)
  def unapply[T <: Instruction](instruction: T): Option[Int] =
    instruction match {
      case chopFrameInstruction: ChopFrameInstruction => Some(chopFrameInstruction.numberOfAbsentLocals)
      case _ => None
    }
}

