package rip.hippo.hippocafe.analaysis.impl

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.analaysis.MaxStackDepthCalculator
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.impl.{BranchInstruction, LabelInstruction}

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 2/22/21
 * @since 1.0.0
 */
final class StandardMaxStackDepthCalculator extends MaxStackDepthCalculator {


  override def calculateMaxStack(methodInfo: MethodInfo): Int = {
    var currentBlock = Block()
    val jumpableBlocks = mutable.Map[LabelInstruction, Block]()
    val blocks = ListBuffer[Block]()

    def addBlock(): Unit = {
      if (currentBlock.instructions.nonEmpty) {
        currentBlock.getStartingLabel match {
          case Some(value) =>
            jumpableBlocks += (value -> currentBlock)
          case None =>
        }
        blocks += currentBlock
        currentBlock = Block()
      }
    }

    methodInfo.instructions.foreach {
      case labelInstruction: LabelInstruction =>
        addBlock()
        currentBlock.instructions += labelInstruction
      case branchInstruction: BranchInstruction =>
        currentBlock.instructions += branchInstruction
        addBlock()
      case instruction =>
        currentBlock.instructions += instruction
    }
    addBlock()

    blocks.foreach(block => {
      val carry = block.getBlockDepth._2
      block.getEndingBranch match {
        case Some(value) =>
          jumpableBlocks.get(value.branch) match {
            case Some(value) =>
              value.setInherit(carry)
            case None =>
          }
        case None =>
      }
    })

    blocks.map(_.getBlockDepth._1).max
  }
}

sealed case class Block() {
  val instructions: ListBuffer[Instruction] = ListBuffer[Instruction]()
  private var inherit = 0

  def getBlockDepth: (Int, Int) = {
    var maxDepth = 0
    var depth = 0

    instructions.foreach(instruction => {
      depth += getInstructionDepth(instruction)
      maxDepth = Math.max(maxDepth, depth)
    })

    (maxDepth + inherit, depth)
  }

  def setInherit(inherit: Int): Unit =
    this.inherit = Math.max(this.inherit, inherit)

  def getStartingLabel: Option[LabelInstruction] =
    instructions.head match {
      case labelInstruction: LabelInstruction => Option(labelInstruction)
      case _ => Option.empty
    }

  def getEndingBranch: Option[BranchInstruction] =
    instructions.last match {
      case branchInstruction: BranchInstruction => Option(branchInstruction)
      case _ => Option.empty
    }

  def getInstructionDepth(instruction: Instruction): Int = {
    0
  }
}