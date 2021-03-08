package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 2/26/21
 * @since 1.0.0
 */
final case class PreprocessedBranch(indexToBranch: UniqueByte, label: LabelInstruction) {
  var offset: Int = 0
  val shiftHistory: ListBuffer[UniqueByte] = ListBuffer[UniqueByte]()

  def shift(bits: Int): UniqueByte = {
    val uniqueByte = UniqueByte(((offset >>> bits) & 0xFF).toByte)
    shiftHistory += uniqueByte
    uniqueByte
  }

  def getSize: Int = if (offset > Short.MaxValue - 4 || offset < Short.MinValue + 4) 4 else 2
}
