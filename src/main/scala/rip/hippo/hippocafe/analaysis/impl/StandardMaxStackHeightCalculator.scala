package rip.hippo.hippocafe.analaysis.impl

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.analaysis.MaxStackHeightCalculator
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.impl.LabelInstruction

import scala.collection.mutable

/**
 * @author Hippo
 * @version 1.0.0, 2/22/21
 * @since 1.0.0
 */
final class StandardMaxStackHeightCalculator extends MaxStackHeightCalculator {


  override def findMaxStack(methodInfo: MethodInfo): Int = {
    var height = 0
    val stackJumpCarry = mutable.Map[LabelInstruction, Int]()
    val handlers = methodInfo.tryCatchBlocks.map(_.handler).toList


    methodInfo.instructions.foreach {
      case label: LabelInstruction =>
        handlers.find(handlers.contains).foreach(_ => height = 1)
        stackJumpCarry.get(label).filter(_ > 1).foreach(height = _)
      case _ =>
    }

    height
  }

  def getInstructionHeight(instruction: Instruction): Int = {
    0
  }
}
