package rip.hippo.hippocafe.verification.block

import rip.hippo.hippocafe.{ClassFile, MethodInfo}
import rip.hippo.hippocafe.disassembler.instruction.impl.{BranchInstruction, LabelInstruction, LookupSwitchInstruction, TableSwitchInstruction}

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/17/21
 * @since 2.0.2
 */
final case class BlockService(classFile: ClassFile, methodInfo: MethodInfo) {
  var currentBlock: Block = Block(classFile, methodInfo)
  val jumpableBlocks: mutable.Map[LabelInstruction, Block] = mutable.Map[LabelInstruction, Block]()
  val blocks: ListBuffer[Block] = ListBuffer[Block]()

  def addBlock(): Unit = {
    if (currentBlock.instructions.nonEmpty) {
      currentBlock.getStartingLabel match {
        case Some(value) =>
          jumpableBlocks += (value -> currentBlock)
        case None =>
      }
      blocks += currentBlock
      currentBlock = Block(classFile, methodInfo)
    }
  }

  methodInfo.instructions.foreach {
    case labelInstruction: LabelInstruction =>
      addBlock()
      currentBlock.instructions += labelInstruction
    case branchInstruction @ (_:BranchInstruction | _:TableSwitchInstruction | _:LookupSwitchInstruction) =>
      currentBlock.instructions += branchInstruction
      addBlock()
    case instruction =>
      currentBlock.instructions += instruction
  }
  addBlock()

  blocks.foreach(block => {
    val carry = block
    block.getEndingBranch match {
      case Some(value) =>
        value.getLabels.foreach(label => {
          jumpableBlocks.get(label) match {
            case Some(value) =>
              value.addInherit(carry)
            case None =>
          }
        })
      case None =>
    }
  })
}

sealed trait BranchingOperation {
  def getLabels: Array[LabelInstruction]
}

final case class BranchBranchingOperation(branchInstruction: BranchInstruction) extends BranchingOperation {
  override def getLabels: Array[LabelInstruction] = Array(branchInstruction.label)
}

final case class TableSwitchBranchingOperation(tableSwitchInstruction: TableSwitchInstruction) extends BranchingOperation {
  private val toArray = tableSwitchInstruction.getLabels.toArray

  override def getLabels: Array[LabelInstruction] = toArray
}

final case class LookupSwitchBranchingOperation(lookupSwitchInstruction: LookupSwitchInstruction) extends BranchingOperation {
  private val toArray = lookupSwitchInstruction.getLabels.toArray

  override def getLabels: Array[LabelInstruction] = toArray
}


