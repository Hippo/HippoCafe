package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.AssemblerContext
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}

/**
 * @author Hippo
 * @version 1.0.0, 2/26/21
 * @since 1.0.0
 */
final case class LineNumberInstruction(var number: Int) extends Instruction {
  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    assemblerContext.labelQueue += this
  }

  override def getOpcode: Option[BytecodeOpcode] = Option.empty
}
