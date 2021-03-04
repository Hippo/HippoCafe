package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.attribute.impl.{LineNumberTableAttribute, StackMapTableAttribute}
import rip.hippo.hippocafe.attribute.impl.data.LineNumberTableAttributeData
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, FrameInstruction}
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction
import rip.hippo.hippocafe.stackmap.StackMapFrame

import scala.collection.mutable
import scala.collection.mutable.{ArrayBuffer, ListBuffer}

/**
 * @author Hippo
 * @version 1.0.1, 2/19/21
 * @since 1.0.0
 */
final class AssemblerContext(flags: Set[AssemblerFlag]) {
  val code: ListBuffer[Byte] = ListBuffer[Byte]()
  val labelToByteOffset: mutable.Map[LabelInstruction, Int] = mutable.Map[LabelInstruction, Int]()
  val preprocessedBranches: ListBuffer[PreprocessedBranch] = ListBuffer[PreprocessedBranch]()
  val lineNumberOffsets: mutable.Map[Int, Int] = mutable.Map[Int, Int]()
  val preprocessedFrames: mutable.Map[FrameInstruction, Int] = mutable.Map[FrameInstruction, Int]()
  var sortedFrames: Map[FrameInstruction, Int] = _
  val stackMapFrames: ListBuffer[StackMapFrame] = ListBuffer[StackMapFrame]()
  var maxStack = 0
  var maxLocals = 0
  var offsetDelta = 0
  var deltaChanges = 0


  def processBranchOffsets(): Unit = {
    // Insert first time calculated offsets
    preprocessedBranches.foreach(preprocessedBranch => {
      val label = preprocessedBranch.label
      val opcodeIndex = preprocessedBranch.indexToBranch
      val labelOffset = labelToByteOffset(label)
      preprocessedBranch.offset = labelOffset - opcodeIndex

      labelToByteOffset.filter(_._2 > opcodeIndex).foreach(pair => labelToByteOffset += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      preprocessedBranches.filter(_.indexToBranch > opcodeIndex).foreach(_.indexToBranch += preprocessedBranch.getSize)
      preprocessedFrames.filter(_._2 > opcodeIndex).foreach(pair => preprocessedFrames += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      lineNumberOffsets.filter(_._2 > opcodeIndex).foreach(pair => lineNumberOffsets += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
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

  def assembleMethodAttributes: Array[AttributeInfo] = {
    val attributes = ArrayBuffer[AttributeInfo]()

    val lineNumberAttributeDataInfo = ArrayBuffer[LineNumberTableAttributeData]()
    lineNumberOffsets.foreach(entry => {
      val lineNumber = entry._1
      val startPc = entry._2
      lineNumberAttributeDataInfo += LineNumberTableAttributeData(startPc, lineNumber)
    })

    sortedFrames = preprocessedFrames.toSeq.sortWith(_._2 < _._2).toMap
    sortedFrames.foreach(_._1.assemble(this))

    attributes += LineNumberTableAttribute(lineNumberAttributeDataInfo.length, lineNumberAttributeDataInfo.toArray)
    attributes += StackMapTableAttribute(stackMapFrames.length, stackMapFrames.toArray)

    attributes.toArray
  }

  def setMaxStack(size: Int): Unit = maxStack = Math.max(maxStack, size)
  def setMaxLocals(size: Int): Unit = maxLocals = Math.max(maxLocals, size)

  def calculateMaxes: Boolean = flags.contains(AssemblerFlag.CALCULATE_MAXES)
  def generateFrames: Boolean = flags.contains(AssemblerFlag.GENERATE_FRAMES)


  def nextDelta(frameInstruction: FrameInstruction): Int = {
    deltaChanges += 1
    val length = sortedFrames(frameInstruction)
    val next = length - offsetDelta - (if (deltaChanges > 1) 1 else 0)
    offsetDelta = length
    next
  }
}
