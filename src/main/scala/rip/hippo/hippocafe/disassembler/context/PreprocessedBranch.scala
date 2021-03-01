package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction

/**
 * @author Hippo
 * @version 1.0.0, 2/26/21
 * @since 1.0.0
 */
final case class PreprocessedBranch(var indexToBranch: Int, label: LabelInstruction) {
  var offset: Int = 0

  def getSize: Int = if (offset > Short.MaxValue - 4 || offset < Short.MinValue + 4) 4 else 2
}
