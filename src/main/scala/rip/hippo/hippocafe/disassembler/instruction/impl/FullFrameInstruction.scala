package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo

/**
 * @author Hippo
 * @version 1.0.0, 3/3/21
 * @since 1.0.0
 */
final case class FullFrameInstruction(locals: Array[VerificationTypeInfo], stack: Array[VerificationTypeInfo]) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {

  }
}

