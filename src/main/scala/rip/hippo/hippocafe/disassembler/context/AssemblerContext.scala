package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
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
  val preprocessedBranches: ListBuffer[PreprocessedBranch] = ListBuffer[PreprocessedBranch]()
  var maxStack = 0
  var maxLocals = 0


  def processJumpOffsets(): Unit = {
    // Insert first time calculated offsets
    preprocessedBranches.foreach(preprocessedBranch => {
      val label = preprocessedBranch.label
      val opcodeIndex = preprocessedBranch.indexToBranch
      val labelOffset = labelToByteOffset(label)
      preprocessedBranch.offset = labelOffset - opcodeIndex

      labelToByteOffset.filter(_._2 > opcodeIndex).foreach(pair => labelToByteOffset += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      preprocessedBranches.filter(_.indexToBranch > opcodeIndex).foreach(_.indexToBranch += preprocessedBranch.getSize)
    })

    // re-align offsets
    var shouldRealign = false
    do {
      shouldRealign = false
      preprocessedBranches.foreach(preprocessedBranch => {
        val startingSize = preprocessedBranch.getSize
        val labelOffset = labelToByteOffset(preprocessedBranch.label)
        preprocessedBranch.offset = labelOffset - preprocessedBranch.indexToBranch
        if (startingSize != preprocessedBranch.getSize) {
          shouldRealign = true
        }
      })
    } while(shouldRealign)

    // write branch instruction
    preprocessedBranches.foreach(preprocessedBranch => {
      val opcodeIndex = preprocessedBranch.indexToBranch

      def shift(bits: Int): Byte = ((preprocessedBranch.offset >>> bits) & 0xFF).toByte

      val wide = preprocessedBranch.getSize == 4

      val opcode = BytecodeOpcode.fromOpcode(code(opcodeIndex)).get match {
        case GOTO if wide => GOTO_W
        case GOTO_W if !wide => GOTO
        case JSR if wide => JSR_W
        case JSR_W if !wide => JSR
        case x => x
      }
      code.update(opcodeIndex, opcode.id.toByte)
      if (wide) {
        code.insert(opcodeIndex + 1, shift(24))
        code.insert(opcodeIndex + 2, shift(16))
        code.insert(opcodeIndex + 3, shift(8))
        code.insert(opcodeIndex + 4, shift(0))
      } else {
        code.insert(opcodeIndex + 1, shift(8))
        code.insert(opcodeIndex + 2, shift(0))
      }
    })
  }

  def setMaxStack(size: Int): Unit = maxStack = Math.max(maxStack, size)
  def setMaxLocals(size: Int): Unit = maxLocals = Math.max(maxLocals, size)

  def calculateMaxes: Boolean = flags.contains(AssemblerFlag.CALCULATE_MAXES)
  def generateFrames: Boolean = flags.contains(AssemblerFlag.GENERATE_FRAMES)
}
