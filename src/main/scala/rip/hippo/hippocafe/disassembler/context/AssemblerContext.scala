package rip.hippo.hippocafe.disassembler.context

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 2/19/21
 * @since 1.0.0
 */
class AssemblerContext(flags: Set[AssemblerFlag]) {
  val code: ListBuffer[Byte] = ListBuffer[Byte]()
  var maxStack = 0
  var maxLocals = 0

  def setMaxStack(size: Int): Unit = maxStack = Math.max(maxStack, size)
  def setMaxLocals(size: Int): Unit = maxLocals = Math.max(maxLocals, size)

  def calculateMaxes: Boolean = flags.contains(AssemblerFlag.CALCULATE_MAXES)
  def generateFrames: Boolean = flags.contains(AssemblerFlag.GENERATE_FRAMES)
}
