package rip.hippo.hippocafe.analaysis.impl

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.analaysis.MaxStackDepthCalculator
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.impl.{ANewArrayInstruction, BranchInstruction, ConstantInstruction, IncrementInstruction, InvokeDynamicInstruction, LabelInstruction, LookupSwitchInstruction, MultiANewArrayInstruction, NewArrayInstruction, PushInstruction, ReferenceInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.*
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.{DoubleConstant, LongConstant}
import rip.hippo.hippocafe.util.Type
import rip.hippo.hippocafe.analaysis.block.{Block, BlockService}

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.1.0, 2/22/21
 * @since 1.0.0
 */
final class StandardMaxStackDepthCalculator extends MaxStackDepthCalculator {


  override def calculateMaxStack(methodInfo: MethodInfo): Int =
    BlockService(methodInfo).blocks.map(_.getBlockDepth._1).max
}
