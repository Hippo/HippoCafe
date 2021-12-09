package rip.hippo.hippocafe.disassembler.context

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.attribute.impl.{LineNumberTableAttribute, LocalVariableTableAttribute, LocalVariableTypeTableAttribute, StackMapTableAttribute}
import rip.hippo.hippocafe.attribute.impl.data.{ExceptionTableAttributeData, LineNumberTableAttributeData, LocalVariableTableAttributeData, LocalVariableTypeTableAttributeData}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, FrameInstruction, Instruction}
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.disassembler.instruction.impl.{LabelInstruction, LineNumberInstruction, LookupSwitchInstruction, TableSwitchInstruction}
import rip.hippo.hippocafe.stackmap.StackMapFrame

import scala.collection.mutable
import scala.collection.mutable.{ArrayBuffer, ListBuffer}

/**
 * @author Hippo
 * @version 1.1.1, 2/19/21
 * @since 1.0.0
 */
final class AssemblerContext(flags: Set[AssemblerFlag]) {
  val code: ListBuffer[UniqueByte] = ListBuffer[UniqueByte]()
  val labelToByte: mutable.Map[Instruction, UniqueByte] = mutable.Map[Instruction, UniqueByte]()
  val labelQueue: ListBuffer[Instruction] = ListBuffer[Instruction]()
  val preprocessedBranches: ListBuffer[PreprocessedBranch] = ListBuffer[PreprocessedBranch]()
  val preprocessedTableSwitch: mutable.Map[TableSwitchInstruction, UniqueByte] = mutable.Map[TableSwitchInstruction, UniqueByte]()
  val preprocessedLookupSwitch: mutable.Map[LookupSwitchInstruction, UniqueByte] = mutable.Map[LookupSwitchInstruction, UniqueByte]()
  val switchPadding: mutable.Map[UniqueByte, Int] = mutable.Map[UniqueByte, Int]()
  var sortedFrames: mutable.LinkedHashMap[FrameInstruction, Int] = mutable.LinkedHashMap[FrameInstruction, Int]()
  val stackMapFrames: ListBuffer[StackMapFrame] = ListBuffer[StackMapFrame]()
  var maxStack = 0
  var maxLocals = 0
  var offsetDelta = 0
  var deltaChanges = 0

  def processBranchOffsets(): Unit = {

    def shift(value: Int, bits: Int): Byte = ((value >>> bits) & 0xFF).toByte

    // Insert first time calculated offsets
    preprocessedBranches.foreach(preprocessedBranch => {
      val label = preprocessedBranch.label
      val opcodeIndex = code.indexOf(preprocessedBranch.indexToBranch)
      val labelOffset = getLabelByte(label)
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
    })

    // re-align offsets
    var shouldRealign = false
    while {
      shouldRealign = false
      switchPadding.foreach(pair => {
        val index = code.indexOf(pair._1)
        val pad = -index & 3
        switchPadding += (pair._1 -> pad)
      })
      preprocessedBranches.foreach(preprocessedBranch => {
        val startingSize = preprocessedBranch.getSize
        val labelOffset = getLabelByte(preprocessedBranch.label)
        val branchIndex = code.indexOf(preprocessedBranch.indexToBranch)
        preprocessedBranch.offset = labelOffset - branchIndex
        val padExtend = switchPadding.filter(pair => {
          val pairIndex = code.indexOf(pair._1)
          val upperBound = Math.max(labelOffset, branchIndex)
          val lowerBound = Math.min(labelOffset, branchIndex)

          pairIndex < upperBound && pairIndex > lowerBound
        }).values.sum
        preprocessedBranch.offset += (if (preprocessedBranch.offset < 0) -padExtend else padExtend)


        if (startingSize != preprocessedBranch.getSize) {
          shouldRealign = true
        }
      })
      shouldRealign
    } do ()

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
      code(opcodeIndex).byte = opcode.opcode.toByte
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
      val indexToInstruction = code.indexOf(pair._2)
      var indexToPad = indexToInstruction + 1
      val padCount = switchPadding(code(indexToPad))
      if (padCount > 0) {
        (0 until padCount).foreach(_ => {
          code.insert(indexToPad, UniqueByte(0))
          indexToPad += 1
        })
      }

      var indexToPairs = indexToPad + 12
      val defaultBranch = getLabelByte(instruction.default)
      val defaultOffset = defaultBranch - indexToInstruction
      code(indexToPad).byte = shift(defaultOffset, 24)
      code(indexToPad + 1).byte = shift(defaultOffset, 16)
      code(indexToPad + 2).byte = shift(defaultOffset, 8)
      code(indexToPad + 3).byte = shift(defaultOffset, 0)
      instruction.table.foreach(label => {
        val labelOffset = getLabelByte(label)
        val offset = labelOffset - indexToInstruction
        code(indexToPairs).byte =  shift(offset, 24)
        code(indexToPairs + 1).byte = shift(offset, 16)
        code(indexToPairs + 2).byte = shift(offset, 8)
        code(indexToPairs + 3).byte = shift(offset, 0)
        indexToPairs += 4
      })
    })

    preprocessedLookupSwitch.foreach(pair => {
      val instruction = pair._1
      val indexToInstruction = code.indexOf(pair._2)
      var indexToPad = indexToInstruction + 1
      val padCount = switchPadding(code(indexToPad))
      if (padCount > 0) {
        (0 until padCount).foreach(_ => {
          code.insert(indexToPad, UniqueByte(0))
          indexToPad += 1
        })
      }

      var indexToPairs = indexToPad + 12
      val defaultBranch = getLabelByte(instruction.default)
      val defaultOffset = defaultBranch - indexToInstruction
      code(indexToPad).byte = shift(defaultOffset, 24)
      code(indexToPad + 1).byte = shift(defaultOffset, 16)
      code(indexToPad + 2).byte = shift(defaultOffset, 8)
      code(indexToPad + 3).byte = shift(defaultOffset, 0)
      instruction.pairs.values.foreach(label => {
        val labelOffset = getLabelByte(label)
        val offset = labelOffset - indexToInstruction
        code(indexToPairs).byte =  shift(offset, 24)
        code(indexToPairs + 1).byte = shift(offset, 16)
        code(indexToPairs + 2).byte = shift(offset, 8)
        code(indexToPairs + 3).byte = shift(offset, 0)
        indexToPairs += 8
      })
    })

  }

  def assembleMethodAttributes(methodInfo: MethodInfo): Array[AttributeInfo] = {
    val attributes = ArrayBuffer[AttributeInfo]()

    val lineNumberAttributeDataInfo = ArrayBuffer[LineNumberTableAttributeData]()

    labelToByte.filter(_._1.isInstanceOf[LineNumberInstruction]).foreach(entry => {
      val lineNumber = entry._1.asInstanceOf[LineNumberInstruction].number
      val startPc = code.indexOf(entry._2)
      lineNumberAttributeDataInfo += LineNumberTableAttributeData(startPc, lineNumber)
    })

    labelToByte.filter(_._1.isInstanceOf[FrameInstruction]).toSeq.sortWith((a, b) => code.indexOf(a._2) < code.indexOf(b._2)).foreach(pair => sortedFrames += (pair._1.asInstanceOf[FrameInstruction] -> code.indexOf(pair._2)))

    sortedFrames.foreach(_._1.finalizeVerificationTypes(this))
    sortedFrames.foreach(_._1.assemble(this))

    attributes += LineNumberTableAttribute(lineNumberAttributeDataInfo.toSeq)
    attributes += StackMapTableAttribute(stackMapFrames.toSeq)

    val localVariableTable = ArrayBuffer[LocalVariableTableAttributeData]()
    val localVariableTypeTable = ArrayBuffer[LocalVariableTypeTableAttributeData]()

    methodInfo.localVariables.foreach(localVariable => {
      val startOffset = getLabelByte(localVariable.start)
      val endOffset = getLabelByte(localVariable.end)
      val length = endOffset - startOffset

      localVariableTable += LocalVariableTableAttributeData(startOffset, length, localVariable.name, localVariable.descriptor, localVariable.index)
      localVariable.signature match {
        case Some(value) =>
          localVariableTypeTable += LocalVariableTypeTableAttributeData(startOffset, length, localVariable.name, value, localVariable.index)
        case None =>
      }
    })

    if (localVariableTable.nonEmpty) {
      attributes += LocalVariableTableAttribute(localVariableTable.toSeq)
    }
    if (localVariableTypeTable.nonEmpty) {
      attributes += LocalVariableTypeTableAttribute(localVariableTypeTable.toSeq)
    }

    attributes.toArray
  }

  def assembleTryCatchBlocks(methodInfo: MethodInfo): Array[ExceptionTableAttributeData] = {
    methodInfo.tryCatchBlocks.map(tcb => ExceptionTableAttributeData(
      getLabelByte(tcb.start),
      getLabelByte(tcb.end),
      getLabelByte(tcb.handler),
      tcb.catchType)).toArray
  }


  def setMaxStack(size: Int): Unit = maxStack = Math.max(maxStack, size)
  def setMaxLocals(size: Int): Unit = maxLocals = Math.max(maxLocals, size)

  def calculateMaxes: Boolean = flags.contains(AssemblerFlag.CALCULATE_MAXES)
  def generateFrames: Boolean = flags.contains(AssemblerFlag.GENERATE_FRAMES)


  private def getLabelByte(labelInstruction: LabelInstruction): Int =
    labelToByte.get(labelInstruction) match {
      case Some(value) => code.indexOf(value)
      case None => code.length
    }

  def nextDelta(frameInstruction: FrameInstruction): Int = {
    deltaChanges += 1
    val length = sortedFrames(frameInstruction)
    val next = length - offsetDelta - (if (deltaChanges > 1) 1 else 0)
    offsetDelta = length
    next
  }
}
