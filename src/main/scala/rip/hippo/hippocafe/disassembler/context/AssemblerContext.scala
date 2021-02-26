package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 2/19/21
 * @since 1.0.0
 */
final class AssemblerContext(flags: Set[AssemblerFlag]) {
  val code: ListBuffer[Byte] = ListBuffer[Byte]()
  val labelToByteOffset: mutable.Map[LabelInstruction, Int] = mutable.Map[LabelInstruction, Int]()
  val postProcessBranchIndexes: mutable.Map[LabelInstruction, ListBuffer[Int]] = mutable.Map[LabelInstruction, ListBuffer[Int]]()
  var maxStack = 0
  var maxLocals = 0

  def setMaxStack(size: Int): Unit = maxStack = Math.max(maxStack, size)
  def setMaxLocals(size: Int): Unit = maxLocals = Math.max(maxLocals, size)

  def calculateMaxes: Boolean = flags.contains(AssemblerFlag.CALCULATE_MAXES)
  def generateFrames: Boolean = flags.contains(AssemblerFlag.GENERATE_FRAMES)
}
