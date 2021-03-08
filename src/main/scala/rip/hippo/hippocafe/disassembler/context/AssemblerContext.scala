package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.attribute.impl.{LineNumberTableAttribute, StackMapTableAttribute}
import rip.hippo.hippocafe.attribute.impl.data.LineNumberTableAttributeData
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, FrameInstruction, Instruction}
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.disassembler.instruction.impl.{LabelInstruction, TableSwitchInstruction}
import rip.hippo.hippocafe.stackmap.StackMapFrame

import scala.collection.mutable
import scala.collection.mutable.{ArrayBuffer, ListBuffer}

/**
 * @author Hippo
 * @version 1.0.1, 2/19/21
 * @since 1.0.0
 */
final class AssemblerContext(flags: Set[AssemblerFlag]) {
  val code: ListBuffer[UniqueByte] = ListBuffer[UniqueByte]()
  val labelToByte: mutable.Map[LabelInstruction, UniqueByte] = mutable.Map[LabelInstruction, UniqueByte]()
  val labelQueue: ListBuffer[LabelInstruction] = ListBuffer[LabelInstruction]()


  val labelToByteOffset: mutable.Map[LabelInstruction, Int] = mutable.Map[LabelInstruction, Int]()
  val preprocessedBranches: ListBuffer[PreprocessedBranch] = ListBuffer[PreprocessedBranch]()
  val lineNumberOffsets: mutable.Map[Int, Int] = mutable.Map[Int, Int]()
  val preprocessedFrames: mutable.Map[FrameInstruction, Int] = mutable.Map[FrameInstruction, Int]()
  val preprocessedTableSwitch: mutable.Map[TableSwitchInstruction, Int] = mutable.Map[TableSwitchInstruction, Int]()
  val switchPadding: mutable.Map[Int, Int] = mutable.Map[Int, Int]()
  var sortedFrames: mutable.LinkedHashMap[FrameInstruction, Int] = mutable.LinkedHashMap[FrameInstruction, Int]()
  val stackMapFrames: ListBuffer[StackMapFrame] = ListBuffer[StackMapFrame]()
  var maxStack = 0
  var maxLocals = 0
  var offsetDelta = 0
  var deltaChanges = 0

  def processBranchOffsets(): Unit = {

    def shift(value: Int, bits: Int): UniqueByte = UniqueByte(((value >>> bits) & 0xFF).toByte)

    // Insert first time calculated offsets
    preprocessedBranches.foreach(preprocessedBranch => {
      val label = preprocessedBranch.label
      val opcodeIndex = code.indexOf(preprocessedBranch.indexToBranch)
      //val labelOffset = labelToByteOffset(label)
      val labelOffset = code.indexOf(labelToByte(label))
      preprocessedBranch.offset = labelOffset - opcodeIndex

      if (preprocessedBranch.getSize == 4) {
        code.insert(opcodeIndex + 1, preprocessedBranch.shift(24))
        code.insert(opcodeIndex + 2, preprocessedBranch.shift(16))
        code.insert(opcodeIndex + 3, preprocessedBranch.shift(8))
        code.insert(opcodeIndex + 4, preprocessedBranch.shift(0))
      } else {
        code.insert(opcodeIndex + 1, preprocessedBranch.shift(8))
        code.insert(opcodeIndex + 2, preprocessedBranch.shift(0))
      }

      //labelToByteOffset.filter(_._2 > opcodeIndex).foreach(pair => labelToByteOffset += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      //preprocessedBranches.filter(_.indexToBranch > opcodeIndex).foreach(_.indexToBranch += preprocessedBranch.getSize)
      /*preprocessedFrames.filter(_._2 > opcodeIndex).foreach(pair => preprocessedFrames += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      lineNumberOffsets.filter(_._2 > opcodeIndex).foreach(pair => lineNumberOffsets += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      preprocessedTableSwitch.filter(_._2 > opcodeIndex).foreach(pair => preprocessedTableSwitch += (pair._1 -> (pair._2 + preprocessedBranch.getSize)))
      switchPadding.filter(_._1 > opcodeIndex).foreach(pair => {
        switchPadding -= pair._1
        switchPadding += (pair._1 + preprocessedBranch.getSize -> pair._2)
      })*/
    })

    // re-align offsets
    var shouldRealign = false
    do {
      shouldRealign = false
     /* switchPadding.foreach(pair => {
        val index = pair._1 + preprocessedBranches.filter(_.indexToBranch < pair._1).map(_.getSize).sum
        val pad = -index & 3
        switchPadding += (pair._1 -> pad)
      })*/
      preprocessedBranches.foreach(preprocessedBranch => {
        val startingSize = preprocessedBranch.getSize
        val labelOffset = code.indexOf(labelToByte(preprocessedBranch.label))
        preprocessedBranch.offset = labelOffset - code.indexOf(preprocessedBranch.indexToBranch)
        /*preprocessedBranch.offset += switchPadding.filter(pair => {
          val upperBound = Math.max(labelOffset, preprocessedBranch.indexToBranch)
          val lowerBound = Math.min(labelOffset, preprocessedBranch.indexToBranch)

          pair._1 < upperBound && pair._1 > lowerBound
        }).values.sum*/


        if (startingSize != preprocessedBranch.getSize) {
          shouldRealign = true
          val difference = preprocessedBranch.getSize - startingSize
          //labelToByteOffset.filter(_._2 > preprocessedBranch.indexToBranch).foreach(pair => labelToByteOffset += (pair._1 -> (pair._2 + difference)))
          //preprocessedFrames.filter(_._2 > preprocessedBranch.indexToBranch).foreach(pair => preprocessedFrames += (pair._1 -> (pair._2 + difference)))
          //lineNumberOffsets.filter(_._2 > preprocessedBranch.indexToBranch).foreach(pair => lineNumberOffsets += (pair._1 -> (pair._2 + difference)))
         // preprocessedBranches.filter(_.indexToBranch > preprocessedBranch.indexToBranch).foreach(_.indexToBranch += difference)
          //preprocessedTableSwitch.filter(_._2 > preprocessedBranch.indexToBranch).foreach(pair => preprocessedTableSwitch += (pair._1 -> (pair._2 + difference)))
          /*switchPadding.filter(_._1 > preprocessedBranch.indexToBranch).foreach(pair => {
            switchPadding -= pair._1
            switchPadding += (pair._1 + difference -> pair._2)
          })*/
        }
      })
    } while(shouldRealign)

    // write branch instruction
    preprocessedBranches.foreach(preprocessedBranch => {

      preprocessedBranch.shiftHistory.foreach(history => code.remove(code.indexOf(history)))

      val opcodeIndex = code.indexOf(preprocessedBranch.indexToBranch)


      def shift(bits: Int): UniqueByte = UniqueByte(((preprocessedBranch.offset >>> bits) & 0xFF).toByte)


      val wide = preprocessedBranch.getSize == 4

      val opcode = BytecodeOpcode.fromOpcode(code(opcodeIndex).byte).get match {
        case GOTO if wide => GOTO_W
        case GOTO_W if !wide => GOTO
        case JSR if wide => JSR_W
        case JSR_W if !wide => JSR
        case x => x
      }
      code.update(opcodeIndex, UniqueByte(opcode.id.toByte))
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

    preprocessedTableSwitch.foreach(pair => {
      val instruction = pair._1
      val indexToInstruction = pair._2
      var indexToPad = indexToInstruction + 1
      val padCount = switchPadding(indexToPad)
      if (padCount > 0) {
        (0 until padCount).foreach(_ => {
          code.insert(indexToPad, UniqueByte(0))
          indexToPad += 1
        })

        labelToByteOffset.filter(_._2 > indexToPad).foreach(pair => labelToByteOffset += (pair._1 -> (pair._2 + padCount)))
        preprocessedFrames.filter(_._2 > indexToPad).foreach(pair => preprocessedFrames += (pair._1 -> (pair._2 + padCount)))
        lineNumberOffsets.filter(_._2 > indexToPad).foreach(pair => lineNumberOffsets += (pair._1 -> (pair._2 + padCount)))
       // preprocessedBranches.filter(_.indexToBranch > indexToPad).foreach(_.indexToBranch += padCount)
        preprocessedTableSwitch.filter(_._2 > indexToPad).foreach(pair => preprocessedTableSwitch += (pair._1 -> (pair._2 + padCount)))
        switchPadding.filter(_._1 > indexToPad).foreach(pair => {
          switchPadding -= pair._1
          switchPadding += (pair._1 + padCount -> pair._2)
        })
      }

      var indexToPairs = indexToPad + 12
      val defaultBranch = labelToByteOffset(instruction.default)
      val defaultOffset = defaultBranch - indexToInstruction
      code.update(indexToPad, shift(defaultOffset, 24))
      code.update(indexToPad + 1, shift(defaultOffset, 16))
      code.update(indexToPad + 2, shift(defaultOffset, 8))
      code.update(indexToPad + 3, shift(defaultOffset, 0))
      instruction.table.foreach(label => {
        val labelOffset = labelToByteOffset(label)
        val offset = labelOffset - indexToInstruction
        code.update(indexToPairs, shift(offset, 24))
        code.update(indexToPairs + 1, shift(offset, 16))
        code.update(indexToPairs + 2, shift(offset, 8))
        code.update(indexToPairs + 3, shift(offset, 0))
        indexToPairs += 4
      })
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

    preprocessedFrames.toSeq.sortWith(_._2 < _._2).foreach(pair => sortedFrames += (pair._1 -> pair._2))
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
