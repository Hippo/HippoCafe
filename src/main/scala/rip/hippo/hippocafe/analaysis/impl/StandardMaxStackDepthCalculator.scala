package rip.hippo.hippocafe.analaysis.impl

import rip.hippo.hippocafe.MethodInfo
import rip.hippo.hippocafe.analaysis.MaxStackDepthCalculator
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.impl.{ANewArrayInstruction, BranchInstruction, ConstantInstruction, IncrementInstruction, LabelInstruction, LookupSwitchInstruction, MultiANewArrayInstruction, NewArrayInstruction, PushInstruction, ReferenceInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.util.Type

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
      print(instruction + " " + depth + " -> ")
      depth += getInstructionDepth(instruction)
      println(depth)
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

  def getInstructionDepth(instruction: Instruction): Int =
    instruction match {
      case ANewArrayInstruction(descriptor) => 0
      case BranchInstruction(bytecodeOpcode, branch) =>
        bytecodeOpcode match {
          case IFEQ | IFNE | IFLT | IFLE | IFGT | IFGE | IFNONNULL | IFNULL => -1
          case IF_ICMPEQ | IF_ICMPGE | IF_ICMPGT | IF_ICMPLE | IF_ICMPLT | IF_ICMPNE => -2
          case JSR | JSR_W => 1
          case _ => 0
        }
      case ConstantInstruction(constant) =>
        constant match {
          case _: Long | Double => 2
          case _ => 1
        }
      case LookupSwitchInstruction(_) => -1
      case MultiANewArrayInstruction(descriptor, dimensions) => 0
      case NewArrayInstruction(arrayType) => 0
      case PushInstruction(_) => 1
      case ReferenceInstruction(bytecodeOpcode, _, _, descriptor) =>
        bytecodeOpcode match {
          case GETSTATIC => Type.getType(descriptor).getSize
          case PUTSTATIC => -Type.getType(descriptor).getSize
          case GETFIELD => Type.getType(descriptor).getSize - 1
          case PUTFIELD => Type.getType(descriptor).getSize + 1
          case INVOKEVIRTUAL | INVOKESPECIAL | INVOKEINTERFACE => getMethodStackDepth(instruction) - 1
          case INVOKESTATIC => getMethodStackDepth(instruction)
        }
      case SimpleInstruction(bytecodeOpcode) => 
        bytecodeOpcode match {
          case ALOAD_0 | ALOAD_1 | ALOAD_2 | ALOAD_3 |
               ILOAD_0 | ILOAD_1 | ILOAD_2 | ILOAD_3 |
               FLOAD_0 | FLOAD_1 | FLOAD_2 | FLOAD_3 |
               ACONST_NULL |
               ICONST_M1 | ICONST_0 | ICONST_1 | ICONST_2 | ICONST_3 | ICONST_4 | ICONST_5 |
               DUP | DUP_X1 | DUP_X2 |
               FCONST_0 | FCONST_1 | FCONST_2 |
               I2L | I2D | F2L | F2D => 1

          case ASTORE_0 | ASTORE_1 | ASTORE_2 | ASTORE_3 |
               ISTORE_0 | ISTORE_1 | ISTORE_2 | ISTORE_3 |
               FSTORE_0 | FSTORE_1 | FSTORE_2 | FSTORE_3 |
               IALOAD | FALOAD | AALOAD | BALOAD | CALOAD | SALOAD |
               POP |
               IADD | FADD |
               ISUB | FSUB |
               IMUL | FMUL |
               IDIV | FDIV |
               IREM | FREM |
               ISHL | ISHR | IUSHR | LSHR | LSHL | LUSHR |
               IAND | IOR | IXOR |
               L2I | L2F | D2I | D2F |
               FCMPL | FCMPG |
               IRETURN | FRETURN | ARETURN |
               ATHROW |
               MONITORENTER | MONITOREXIT => -1

          case _ => 0
        }
      case TableSwitchInstruction(_, _, _) => -1
      case TypeInstruction(bytecodeOpcode, _) if bytecodeOpcode == NEW => 1
      case variable: VariableInstruction =>
        if (variable.isLoad) {
          if (variable.bytecodeOpcode == DLOAD || variable.bytecodeOpcode == LLOAD) 2 else 1
        } else {
          if (variable.bytecodeOpcode == DSTORE || variable.bytecodeOpcode == LSTORE) -2 else -1
        }
      case _ => 0
    }

  private def getMethodStackDepth(instruction: Instruction): Int = {
    var size = 0
    val parameters: ListBuffer[Type] = ListBuffer[Type]()
    var returnType: Type = null
    instruction match {
      case ReferenceInstruction(_, _, _, descriptor) =>
        parameters.addAll(Type.getMethodParameterTypes(descriptor))
        returnType = Type.getMethodReturnType(descriptor)
      //TODO: Invoke dynamic
    }
    parameters.foreach(size -= _.getSize)
    size += returnType.getSize
    size
  }
}