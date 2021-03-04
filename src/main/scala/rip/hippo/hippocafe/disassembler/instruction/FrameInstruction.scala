package rip.hippo.hippocafe.disassembler.instruction

import rip.hippo.hippocafe.disassembler.context.AssemblerContext

/**
 * @author Hippo
 * @version 1.0.0, 3/4/21
 * @since 1.0.0
 */
trait FrameInstruction {
  def assemble(assemblerContext: AssemblerContext): Unit
}
