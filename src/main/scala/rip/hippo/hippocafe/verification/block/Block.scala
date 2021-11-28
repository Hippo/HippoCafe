package rip.hippo.hippocafe.verification.block

import rip.hippo.hippocafe.{ClassFile, MethodInfo}
import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.*
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.{DoubleConstant, LongConstant}
import rip.hippo.hippocafe.disassembler.instruction.impl.{ANewArrayInstruction, BranchInstruction, ConstantInstruction, InvokeDynamicInstruction, LabelInstruction, LookupSwitchInstruction, MultiANewArrayInstruction, PushInstruction, ReferenceInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.hippocafe.disassembler.tcb.TryCatchBlock
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.{DoubleVerificationTypeInfo, FloatVerificationTypeInfo, IntegerVerificationTypeInfo, LongVerificationTypeInfo, ObjectVerificationTypeInfo}
import rip.hippo.hippocafe.util.Type
import rip.hippo.hippocafe.util.Type.*
import rip.hippo.hippocafe.verification.frame.VirtualFrame

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/17/21
 * @since 2.0.2
 */
final case class Block(classFile: ClassFile, methodInfo: MethodInfo) {
  val instructions: ListBuffer[Instruction] = ListBuffer[Instruction]()
  val inheritBlocks: ListBuffer[Block] = ListBuffer[Block]()
  /*private val locals = mutable.Map[Int, VerificationTypeInfo]()
  
  var index = 0
  if (!methodInfo.accessFlags.contains(AccessFlag.ACC_STATIC)) {
    locals += (index -> ObjectVerificationTypeInfo(classFile.name))
    index += 1
  }
  Type.getMethodParameterTypes(methodInfo.descriptor).foreach {
    case BOOLEAN | BYTE | CHAR | SHORT | INT =>
      locals += (index -> new IntegerVerificationTypeInfo)
      index += 1
    case FLOAT =>
      locals += (index -> new FloatVerificationTypeInfo)
      index += 1
    case LONG =>
      locals += (index -> new LongVerificationTypeInfo)
      index += 2
    case DOUBLE =>
      locals += (index -> new DoubleVerificationTypeInfo)
      index += 2
    case x =>
      locals += (index -> ObjectVerificationTypeInfo(x.descriptor))
      index += 1
  }
  
  var virtualFrame: VirtualFrame = VirtualFrame(locals)*/
  
  private var inheritStack = 0

  def getBlockDepth: (Int, Int) = {
    var maxDepth = 0
    var depth = 0

    instructions.foreach(instruction => {

      instruction match {
        case labelInstruction: LabelInstruction =>
          if (methodInfo.tryCatchBlocks.map(_.handler).contains(labelInstruction)) {
            depth = 1
          }
        case _ =>
      }

      depth += getInstructionDepth(instruction)
      maxDepth = Math.max(maxDepth, depth)
    })

    (maxDepth + inheritStack, depth)
  }
  
  def findParentBlock(blockService: BlockService): Option[Block] =
    getStartingLabel match {
      case Some(label) =>
        blockService.blocks.find(block => {
          block.getEndingBranch match {
            case Some(value) => value.getLabels.exists(_.equals(label))
            case None => false
          }
        })
      case None => Option.empty
    }

  def requiresFrame(): Boolean =
    getStartingLabel match {
      case Some(value) =>
        methodInfo.instructions.exists {
          case branchInstruction: BranchInstruction => branchInstruction.label == value
          case tableSwitchInstruction: TableSwitchInstruction => tableSwitchInstruction.getLabels.contains(value)
          case lookupSwitchInstruction: LookupSwitchInstruction => lookupSwitchInstruction.getLabels.contains(value)
          case _ => false
        } || methodInfo.tryCatchBlocks.exists(_.handler == value)
      case None => false
    }

  def isTcbHandler: Boolean =
    getStartingLabel match {
      case Some(value) => methodInfo.tryCatchBlocks.exists(_.handler == value)
      case None => false
    }

  def getAssociatedTcb: Option[TryCatchBlock] =
    methodInfo.tryCatchBlocks.find(tcb => getStartingLabel.contains(tcb.handler) || getStartingLabel.contains(tcb.start) || getStartingLabel.contains(tcb.end))

  def addInherit(inherit: Block): Unit = {
    this.inheritStack = Math.max(this.inheritStack, inherit.inheritStack)
    this.inheritBlocks += inherit
  }

  def getStartingLabel: Option[LabelInstruction] =
    instructions.head match {
      case labelInstruction: LabelInstruction => Option(labelInstruction)
      case _ => Option.empty
    }

  def getEndingBranch: Option[BranchingOperation] =
    instructions.last match {
      case branchInstruction: BranchInstruction => Option(BranchBranchingOperation(branchInstruction))
      case lookupSwitchInstruction: LookupSwitchInstruction => Option(LookupSwitchBranchingOperation(lookupSwitchInstruction))
      case tableSwitchInstruction: TableSwitchInstruction => Option(TableSwitchBranchingOperation(tableSwitchInstruction))
      case _ => Option.empty
    }

  def isGoto: Boolean =
    instructions.last match {
      case branch: BranchInstruction => branch.bytecodeOpcode == BytecodeOpcode.GOTO
      case _ => false
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
          case _: LongConstant => 2
          case _: DoubleConstant => 2
          case _ => 1
        }
      case LookupSwitchInstruction(_) => -1
      case MultiANewArrayInstruction(_, dimensions) => -dimensions + 1
      case PushInstruction(_) => 1
      case ReferenceInstruction(bytecodeOpcode, _, _, descriptor) =>
        bytecodeOpcode match {
          case GETSTATIC => Type.getType(descriptor).getSize
          case PUTSTATIC => -Type.getType(descriptor).getSize
          case GETFIELD => Type.getType(descriptor).getSize - 1
          case PUTFIELD => -Type.getType(descriptor).getSize + 1
          case INVOKEVIRTUAL | INVOKESPECIAL | INVOKEINTERFACE => getMethodStackDepth(instruction) - 1
          case INVOKESTATIC => getMethodStackDepth(instruction)
        }
      case InvokeDynamicInstruction(invokeDynamicConstant) => getMethodStackDepth(instruction)
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

          case LCONST_0 | LCONST_1 | DCONST_0 | DCONST_1 |
               LLOAD_0 | LLOAD_1 | LLOAD_2 | LLOAD_3 |
               DLOAD_0 | DLOAD_1 | DLOAD_2 | DLOAD_3 |
               DUP2 | DUP2_X1 | DUP2_X2 => 2

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

          case LSTORE_0 | LSTORE_1 | LSTORE_2 | LSTORE_3 |
               DSTORE_0 | DSTORE_1 | DSTORE_2 | DSTORE_3 |
               POP2 |
               LADD | LSUB | LMUL | LDIV | LREM | LAND | LOR | LXOR |
               DADD | DSUB | DMUL | DDIV | DREM |
               LRETURN | DRETURN => -2

          case LCMP | DCMPL | DCMPG |
               IASTORE | FASTORE | AASTORE | BASTORE | CASTORE | SASTORE => -3

          case DASTORE | LASTORE => -4

          case _ => 0
        }
      case TableSwitchInstruction(_, _, _) => -1
      case TypeInstruction(bytecodeOpcode, _) if bytecodeOpcode == NEW => 1
      case variable: VariableInstruction =>
        if (variable.bytecodeOpcode == RET) 0
        else if (variable.isLoad) {
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
      case InvokeDynamicInstruction(invokeDynamicConstant) =>
        parameters.addAll(Type.getMethodParameterTypes(invokeDynamicConstant.descriptor))
        returnType = Type.getMethodReturnType(invokeDynamicConstant.descriptor)
    }
    parameters.foreach(size -= _.getSize)
    size += returnType.getSize
    size
  }
}