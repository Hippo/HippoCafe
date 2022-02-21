package rip.hippo.hippocafe.verification.frame

import rip.hippo.hippocafe.{ClassFile, MethodInfo}
import rip.hippo.hippocafe.constantpool.info.impl.FloatInfo
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.*
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.*
import rip.hippo.hippocafe.disassembler.instruction.impl.*
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.*
import rip.hippo.hippocafe.util.Type
import rip.hippo.hippocafe.disassembler.instruction.array.ArrayType
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.verification.MethodVerificationService

import scala.collection.mutable

/**
 * @author Hippo
 * @version 1.0.0, 8/17/21
 * @since 2.0.0
 */
final case class VirtualFrame(localVariables: mutable.Map[Int, VerificationTypeInfo], methodVerificationService: MethodVerificationService) {

  val stack: mutable.Stack[VerificationTypeInfo] = mutable.Stack[VerificationTypeInfo]()

  def merge(other: VirtualFrame): VirtualFrame = {
    val locals = mutable.Map[Int, VerificationTypeInfo]()
    locals ++= this.localVariables
    other.localVariables.foreach((index, info) => {
      locals.get(index) match {
        case Some(value) =>
          if (value.getClass != info.getClass) throw new HippoCafeException(s"Inconsistent locals, expected ${value.getClass} at $index, got ${info.getClass}")
          value match {
            case ObjectVerificationTypeInfo(name) =>
              locals += (index -> ObjectVerificationTypeInfo(internalizeObjectVerification(methodVerificationService.getCommonSuperType(name, info.asInstanceOf[ObjectVerificationTypeInfo].name))))

            case _ => // ignore, we already got it in the locals
          }
        case None =>
          locals += (index -> info) // this probably shouldn't happen
      }
    })

    if (other.stack.size != this.stack.size) throw new HippoCafeException("Inconsistent stack size.")


    val frame = VirtualFrame(localVariables, methodVerificationService)

    this.stack.indices.foreach(i => {
      val first = this.stack(i)
      val second = other.stack(i)
      if (first.getClass != second.getClass) throw new HippoCafeException(s"Inconsistent stack, expected ${first.getClass}, got ${second.getClass}")
      val info = (first, second) match {
        case (obj1: ObjectVerificationTypeInfo, obj2: ObjectVerificationTypeInfo) => ObjectVerificationTypeInfo(internalizeObjectVerification(methodVerificationService.getCommonSuperType(obj1.name, obj2.name)))
        case _ => first
      }

      frame.push(info)
    })

    frame
  }

  def execute(classFile: ClassFile, methodInfo: MethodInfo, instruction: Instruction): Unit = {
    instruction match {
      case ANewArrayInstruction(descriptor) =>
        pop()
        val arrayDescriptor = if (descriptor.charAt(0) == '[') {
          "[".concat(descriptor)
        } else {
          "[L".concat(descriptor).concat(";")
        }
        push(ObjectVerificationTypeInfo(internalizeObjectVerification(arrayDescriptor)))
      case BranchInstruction(bytecodeOpcode, label) =>
        bytecodeOpcode match {
          case IFEQ | IFNE | IFLT | IFGE | IFGT | IFLE | IFNULL | IFNONNULL =>
            pop()
          case IF_ICMPEQ | IF_ICMPNE | IF_ICMPLT | IF_ICMPGE | IF_ICMPGT | IF_ICMPLE | IF_ACMPEQ | IF_ACMPNE =>
            pop(2)
          case _ =>
        }
      case ConstantInstruction(constant) =>
        constant match {
          case ClassConstant(value) =>
            push(ObjectVerificationTypeInfo(internalizeObjectVerification("java/lang/Class")))
          case DoubleConstant(value) =>
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case DynamicConstant(name, descriptor, bootstrapMethod, bootstrapArguments) =>
            push(ObjectVerificationTypeInfo(internalizeObjectVerification(descriptor)))
          case FloatConstant(value) =>
            push(FloatVerificationTypeInfo())
          case IntegerConstant(value) =>
            push(IntegerVerificationTypeInfo())
          case InvokeDynamicConstant(name, descriptor, bootstrapMethod, bootstrapArguments) =>
            push(ObjectVerificationTypeInfo(internalizeObjectVerification(descriptor)))
          case LongConstant(value) =>
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case MethodHandleConstant(referenceKind, owner, name, descriptor, isInterface) =>
            push(ObjectVerificationTypeInfo("java/lang/invoke/MethodHandle"))
          case MethodTypeConstant(value) =>
            push(ObjectVerificationTypeInfo("java/lang/invoke/MethodType"))
          case ModuleConstant(value) =>
            push(ObjectVerificationTypeInfo(internalizeObjectVerification(value)))
          case StringConstant(value) =>
            push(ObjectVerificationTypeInfo("java/lang/String"))
          case UTF8Constant(value) =>
            push(ObjectVerificationTypeInfo("java/lang/String"))
        }
      case IncrementInstruction(localIndex, value) => store(localIndex, IntegerVerificationTypeInfo())
      case InvokeDynamicInstruction(invokeDynamicConstant) =>
        val value = Type.getMethodParameterTypes(invokeDynamicConstant.descriptor)
        value.foreach(t => pop(if (t.isWide) 2 else 1))
        convertAndPush(Type.getMethodReturnType(invokeDynamicConstant.descriptor).descriptor)

      case LookupSwitchInstruction(default) => pop()
      case MultiANewArrayInstruction(descriptor, dimensions) =>
        pop(dimensions)
        push(ObjectVerificationTypeInfo(internalizeObjectVerification(descriptor)))
      case NewArrayInstruction(arrayType) =>
        pop()
        import ArrayType._
        arrayType match {
          case BOOLEAN => push(ObjectVerificationTypeInfo("[Z"))
          case CHAR => push(ObjectVerificationTypeInfo("[C"))
          case FLOAT => push(ObjectVerificationTypeInfo("[F"))
          case DOUBLE => push(ObjectVerificationTypeInfo("[D"))
          case BYTE => push(ObjectVerificationTypeInfo("[B"))
          case SHORT => push(ObjectVerificationTypeInfo("[S"))
          case INT => push(ObjectVerificationTypeInfo("[I"))
          case LONG => push(ObjectVerificationTypeInfo("[J"))
        }
      case PushInstruction(value) =>
        push(IntegerVerificationTypeInfo())
      case ReferenceInstruction(bytecodeOpcode, owner, name, descriptor) =>
        bytecodeOpcode match {
          case GETSTATIC =>
            convertAndPush(descriptor)
          case PUTSTATIC =>
            if (isWideInfo(pop())) pop()
          case GETFIELD =>
            pop()
            convertAndPush(descriptor)
          case PUTFIELD =>
            if (isWideInfo(pop())) pop()
            pop()
          case INVOKEVIRTUAL | INVOKESPECIAL | INVOKESTATIC | INVOKEINTERFACE =>
            val value = Type.getMethodParameterTypes(descriptor)
            value.foreach(t => pop(if (t.isWide) 2 else 1))
            if (bytecodeOpcode != INVOKESTATIC) pop()
            val desc = Type.getMethodReturnType(descriptor).descriptor
            if (!desc.equals("V")) convertAndPush(desc)
            if (bytecodeOpcode == INVOKESPECIAL) {
              if (stack.nonEmpty) {
                val top = stack.head
                top match {
                  case uninitializedVerificationTypeInfo: UninitializedVerificationTypeInfo =>
                    uninitializedVerificationTypeInfo.typeInstruction match {
                      case Some(value) =>
                        pop()
                        push(ObjectVerificationTypeInfo(internalizeObjectVerification(value.typeName)))
                      case None =>
                    }
                  case _ =>
                }
              }
              if (methodInfo.name.equals("<init>")) {
                localVariables.get(0) match {
                  case Some(value: UninitializedThisVerificationTypeInfo) =>
                    store(0, ObjectVerificationTypeInfo(internalizeObjectVerification(classFile.name)))
                  case _ =>
                }
              }
            }
          case _ =>
        }
      case SimpleInstruction(bytecodeOpcode) =>
        bytecodeOpcode match {
          case POP | IRETURN | FRETURN | ARETURN | ATHROW | MONITORENTER | MONITOREXIT =>
            pop()
          case POP2 | LRETURN | DRETURN =>
            pop(2)
          case IASTORE | BASTORE | CASTORE | SASTORE | FASTORE | AASTORE =>
            pop(3)
          case LASTORE | DASTORE =>
            pop(4)
          case ACONST_NULL =>
            push(NullVerificationTypeInfo())
          case ICONST_M1 | ICONST_0 | ICONST_1 | ICONST_2 | ICONST_3 | ICONST_4 | ICONST_5 | ILOAD_0 | ILOAD_1 | ILOAD_2 | ILOAD_3 =>
            push(IntegerVerificationTypeInfo())
          case LCONST_0 | LCONST_1 | LLOAD_0 | LLOAD_1 | LLOAD_2 | LLOAD_3 =>
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case FCONST_0 | FCONST_1 | FCONST_2 | FLOAD_0 | FLOAD_1 | FLOAD_2 | FLOAD_3 =>
            push(FloatVerificationTypeInfo())
          case DCONST_0 | DCONST_1 | DLOAD_0 | DLOAD_1 | DLOAD_2 | DLOAD_3 =>
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case ALOAD_0 | ALOAD_1 | ALOAD_2 | ALOAD_3 =>
            push(load(bytecodeOpcode.opcode - ALOAD_0.opcode))
          case LALOAD | D2L =>
            pop(2)
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case DALOAD | L2D =>
            pop(2)
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case AALOAD =>
            pop()
            val arrayRef = pop()
            arrayRef match {
              case nullVerificationTypeInfo: NullVerificationTypeInfo => push(nullVerificationTypeInfo)
              case objectVerificationTypeInfo: ObjectVerificationTypeInfo => push(ObjectVerificationTypeInfo(internalizeObjectVerification(objectVerificationTypeInfo.name.substring(1))))
              case _ => push(arrayRef) // shouldn't happen
            }
          case ISTORE_0 | ISTORE_1 | ISTORE_2 | ISTORE_3 =>
            pop()
            store(bytecodeOpcode.opcode - ISTORE_0.opcode, IntegerVerificationTypeInfo())
          case FSTORE_0 | FSTORE_1 | FSTORE_2 | FSTORE_3 =>
            pop()
            store(bytecodeOpcode.opcode - FSTORE_0.opcode, FloatVerificationTypeInfo())
          case ASTORE_0 | ASTORE_1 | ASTORE_2 | ASTORE_3 =>
            store(bytecodeOpcode.opcode - ASTORE_0.opcode, pop())
          case LSTORE_0 | LSTORE_1 | LSTORE_2 | LSTORE_3 =>
            val index = bytecodeOpcode.opcode - LSTORE_0.opcode
            pop(2)
            store(index, LongVerificationTypeInfo())
            store(index + 1, TopVerificationTypeInfo())
          case DSTORE_0 | DSTORE_1 | DSTORE_2 | DSTORE_3 =>
            val index = bytecodeOpcode.opcode - DSTORE_0.opcode
            pop(2)
            store(index, DoubleVerificationTypeInfo())
            store(index + 1, TopVerificationTypeInfo())
          case DUP =>
            val pop1 = pop()
            push(pop1)
            push(pop1)
          case DUP_X1 =>
            val pop1 = pop()
            val pop2 = pop()
            push(pop1)
            push(pop2)
            push(pop1)
          case DUP_X2 =>
            val pop1 = pop()
            val pop2 = pop()
            val pop3 = pop()
            push(pop1)
            push(pop3)
            push(pop2)
            push(pop1)
          case DUP2 =>
            val pop1 = pop()
            val pop2 = pop()
            push(pop2)
            push(pop1)
            push(pop2)
            push(pop1)
          case DUP2_X1 =>
            val pop1 = pop()
            val pop2 = pop()
            val pop3 = pop()
            push(pop2)
            push(pop1)
            push(pop3)
            push(pop2)
            push(pop1)
          case DUP2_X2 =>
            val pop1 = pop()
            val pop2 = pop()
            val pop3 = pop()
            val pop4 = pop()
            push(pop2)
            push(pop1)
            push(pop4)
            push(pop3)
            push(pop2)
            push(pop1)
          case SWAP =>
            val pop1 = pop()
            val pop2 = pop()
            push(pop1)
            push(pop2)
          case IALOAD | BALOAD | CALOAD | SALOAD | IADD | ISUB | IMUL | IDIV | IREM | IAND | IOR | IXOR | ISHL | ISHR | IUSHR | L2I | D2I | FCMPL | FCMPG =>
            pop(2)
            push(IntegerVerificationTypeInfo())
          case LADD | LSUB | LMUL | LDIV | LREM | LAND | LOR | LXOR =>
            pop(4)
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case FALOAD | FADD | FSUB | FMUL | FDIV | FREM | L2F | D2F =>
            pop(2)
            push(FloatVerificationTypeInfo())
          case DADD | DSUB | DMUL | DDIV | DREM =>
            pop(4)
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case LSHL | LSHR | LUSHR =>
            pop(3)
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case I2L | F2L =>
            pop(1)
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case I2F =>
            pop(1)
            push(FloatVerificationTypeInfo())
          case I2D | F2D =>
            pop(1)
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case F2I | ARRAYLENGTH =>
            pop(1)
            push(IntegerVerificationTypeInfo())
          case LCMP | DCMPL | DCMPG =>
            pop(4)
            push(IntegerVerificationTypeInfo())
          case _ =>
        }
      case TableSwitchInstruction(default, low, high) => pop()
      case TypeInstruction(bytecodeOpcode, typeName) =>
        bytecodeOpcode match {
          case INSTANCEOF =>
            pop(1)
            push(IntegerVerificationTypeInfo())
          case NEW =>
            push(new UninitializedVerificationTypeInfo(instruction.asInstanceOf[TypeInstruction]))
          case _ =>
        }
      case VariableInstruction(bytecodeOpcode, index) =>
        bytecodeOpcode match {
          case ILOAD =>
            push(IntegerVerificationTypeInfo())
          case LLOAD =>
            push(LongVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case FLOAD =>
            push(FloatVerificationTypeInfo())
          case DLOAD =>
            push(DoubleVerificationTypeInfo())
            push(TopVerificationTypeInfo())
          case ALOAD =>
            push(load(index))
          case ISTORE =>
            pop()
            store(index, IntegerVerificationTypeInfo())
          case FSTORE =>
            pop()
            store(index, FloatVerificationTypeInfo())
          case ASTORE =>
            store(index, pop())
          case LSTORE =>
            pop(2)
            store(index, LongVerificationTypeInfo())
            store(index + 1, TopVerificationTypeInfo())
          case DSTORE =>
            pop(2)
            store(index, DoubleVerificationTypeInfo())
            store(index + 1, TopVerificationTypeInfo())
          case _ =>
        }
      case labelInstruction: LabelInstruction =>
        val tcbTypes = methodInfo.tryCatchBlocks.filter(_.handler == labelInstruction).map(_.safeCatchType)
        if (tcbTypes.nonEmpty) {
          stack.clear()
          val commonType = tcbTypes.foldLeft(tcbTypes.head)((t1, t2) => methodVerificationService.getCommonSuperType(t1, t2))
          push(ObjectVerificationTypeInfo(internalizeObjectVerification(commonType)))
        }

      case _ =>
    }
  }

  def push(verificationTypeInfo: VerificationTypeInfo): Unit =
    stack.push(verificationTypeInfo)

  def pop(slots: Int): Unit =
    (0 until slots).foreach(_ => pop())

  def pop(): VerificationTypeInfo =
    stack.pop()

  def store(index: Int, verificationTypeInfo: VerificationTypeInfo): Unit =
    localVariables += (index -> verificationTypeInfo)

  def load(index: Int): VerificationTypeInfo =
    localVariables(index)
    
  def internalizeObjectVerification(descriptor: String): String = {
    if (descriptor.startsWith("L") && descriptor.endsWith(";")) descriptor.substring(1, descriptor.length - 1) else descriptor
  }

  def convertType(value: Type): VerificationTypeInfo = {
    value match {
      case Type.BOOLEAN | Type.BYTE | Type.CHAR | Type.SHORT | Type.INT => IntegerVerificationTypeInfo()
      case Type.FLOAT => FloatVerificationTypeInfo()
      case Type.LONG => LongVerificationTypeInfo()
      case Type.DOUBLE => DoubleVerificationTypeInfo()
      case x => ObjectVerificationTypeInfo(internalizeObjectVerification(x.descriptor))
    }
  }

  def convertAndPush(descriptor: String): Unit = {
    val convert = convertType(Type.getType(descriptor))
    push(convert)
    if (isWideInfo(convert)) push(TopVerificationTypeInfo())
  }

  def isWideInfo(verificationTypeInfo: VerificationTypeInfo): Boolean = verificationTypeInfo match {
    case _ @ (_: DoubleVerificationTypeInfo | _: LongVerificationTypeInfo) => true
    case _ => false
  }

  override def toString: String =
    s"VirtualFrame($localVariables, $stack)"

  override def equals(obj: Any): Boolean =
    obj match {
      case virtualFrame: VirtualFrame =>
        sameLocals(virtualFrame) && sameStack(virtualFrame)
      case _ => false
    }

  def sameStack(virtualFrame: VirtualFrame): Boolean =
    virtualFrame.getActualStack.size == this.getActualStack.size && virtualFrame.getActualStack.equals(this.getActualStack)


  def sameLocals(virtualFrame: VirtualFrame): Boolean =
    virtualFrame.getActualLocals.size == this.getActualLocals.size && virtualFrame.getActualLocals.equals(this.getActualLocals)


  def getActualLocals: Map[Int, VerificationTypeInfo] = {
    val locals = mutable.Map[Int, VerificationTypeInfo]()

    if (localVariables.isEmpty) locals.toMap
    
    val maxLocalIndex = localVariables.keys.max
    var index = 0
    while (index <= maxLocalIndex) {
      val info = localVariables(index)
      locals += (index -> info)
      info match {
        case _: DoubleVerificationTypeInfo => index += 2
        case _: LongVerificationTypeInfo => index += 2
        case _ => index += 1
      }
    }
    locals.toMap
  }

  def getActualStack: mutable.Stack[VerificationTypeInfo] = {
    stack.zipWithIndex.filter {
      case (topVerificationTypeInfo: TopVerificationTypeInfo, index) if index > 0 => {
        val item = stack(index - 1)
        item match {
          case _: DoubleVerificationTypeInfo => false
          case _: LongVerificationTypeInfo => false
          case _ => true
        }
      }
      case _ => true
    }.map(_._1)
  }
}
