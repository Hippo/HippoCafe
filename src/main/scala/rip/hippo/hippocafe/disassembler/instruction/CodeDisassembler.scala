/*
 * MIT License
 *
 * Copyright (c) 2021 Hippo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

package rip.hippo.hippocafe.disassembler.instruction

import rip.hippo.hippocafe.constantpool.info.impl.{DynamicInfo, ReferenceInfo, StringInfo}
import rip.hippo.hippocafe.disassembler.instruction.impl.{ANewArrayInstruction, AppendFrameInstruction, BranchInstruction, ChopFrameInstruction, ConstantInstruction, FullFrameInstruction, IncrementInstruction, InvokeDynamicInstruction, LabelInstruction, LineNumberInstruction, LookupSwitchInstruction, MultiANewArrayInstruction, NewArrayInstruction, PushInstruction, ReferenceInstruction, SameFrameInstruction, SameLocalsFrameInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.hippocafe.{LocalVariableInfo, MethodInfo}
import rip.hippo.hippocafe.attribute.Attribute
import rip.hippo.hippocafe.attribute.impl.{BootstrapMethodsAttribute, CodeAttribute, LineNumberTableAttribute, LocalVariableTableAttribute, LocalVariableTypeTableAttribute, StackMapTableAttribute}
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.info.ValueAwareness
import rip.hippo.hippocafe.disassembler.instruction.array.ArrayType
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant
import rip.hippo.hippocafe.disassembler.instruction.constant.impl._
import rip.hippo.hippocafe.disassembler.tcb.TryCatchBlock
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.stackmap.impl.{AppendStackMapFrame, ChopStackMapFrame, FullStackMapFrame, SameExtendedStackMapFrame, SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame, SameStackMapFrame}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.UninitializedVerificationTypeInfo

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
object CodeDisassembler {

  sealed case class LocalLookup(name: String, startPc: Int, length: Int, index: Int)

  def disassemble(methodInfo: MethodInfo, constantPool: ConstantPool, bootstrapMethods: Option[BootstrapMethodsAttribute]): Unit = {
    methodInfo.attributes.find(_.kind == Attribute.CODE) match {
      case Some(codeAttribute: CodeAttribute) =>
        val instructions = mutable.SortedMap[Int, Instruction]()
        val tryCatchBlocks = ListBuffer[TryCatchBlock]()
        var offset = 0
        val code = codeAttribute.code
        val exceptions = codeAttribute.exceptionTable
        val codeLength = code.length
        val labels = mutable.SortedMap[Int, ListBuffer[Instruction]]()
        var labelDebugId = 0
        val localLookupMap = mutable.Map[LocalLookup, LocalVariableInfo]()

        def addLabel(offset: Int, instruction: Instruction): Unit = {
          if (!labels.contains(offset)) {
            labels += (offset -> ListBuffer[Instruction]())
          }
          labels(offset) += instruction
        }

        codeAttribute.attributes.foreach {
          case lineNumberTableAttribute: LineNumberTableAttribute =>
            lineNumberTableAttribute.lineNumberTable.foreach(data => addLabel(data.startPc, LineNumberInstruction(data.lineNumber)))
          case LocalVariableTableAttribute(table) =>
            table.foreach(data => {
              val lookup = LocalLookup(data.name, data.startPc, data.length, data.index)
              localLookupMap.get(lookup) match {
                case Some(value) =>
                  value.descriptor = data.descriptor
                case None =>
                  val start = LabelInstruction()
                  val end = LabelInstruction()
                  addLabel(data.startPc, start)
                  addLabel(data.startPc + data.length, end)
                  val info = LocalVariableInfo(data.name, data.descriptor, Option.empty, start, end, data.index)
                  localLookupMap += (lookup -> info)
              }
            })
          case LocalVariableTypeTableAttribute(table) =>
            table.foreach(data => {
              val lookup = LocalLookup(data.name, data.startPc, data.length, data.index)
              localLookupMap.get(lookup) match {
                case Some(value) =>
                  value.signature = Option(data.signature)
                case None =>
                  val start = LabelInstruction()
                  val end = LabelInstruction()
                  addLabel(data.startPc, start)
                  addLabel(data.startPc + data.length, end)
                  val info = LocalVariableInfo(data.name, null /*this will be changed when it reads a `LocalVariableTableAttribute`*/, Option(data.signature), start, end, data.index)
                  localLookupMap += (lookup -> info)
              }
            })

          case stackMapTableAttribute: StackMapTableAttribute =>
            var offsetDelta = 0
            var deltaChanges = 0
            stackMapTableAttribute.entries.foreach {
              case SameStackMapFrame(frameType) =>
                offsetDelta += frameType
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, SameFrameInstruction())
              case SameLocalsStackMapFrame(frameType, stack) =>
                offsetDelta += frameType - 64
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, SameLocalsFrameInstruction(stack(0)))
              case SameLocalsExtendedStackMapFrame(delta, stack) =>
                offsetDelta += delta
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, SameLocalsFrameInstruction(stack(0)))
              case ChopStackMapFrame(frameType, delta) =>
                offsetDelta += delta
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, ChopFrameInstruction(251 - frameType))
              case SameExtendedStackMapFrame(delta) =>
                offsetDelta += delta
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, SameFrameInstruction())
              case AppendStackMapFrame(_, delta, locals) =>
                offsetDelta += delta
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, AppendFrameInstruction(locals))
              case FullStackMapFrame(delta, _, locals, _, stack) =>
                offsetDelta += delta
                deltaChanges += 1
                if (deltaChanges > 1) offsetDelta += 1
                addLabel(offsetDelta, FullFrameInstruction(locals, stack))
              case x => throw new HippoCafeException(s"Unknown stack map frame $x")
            }
          case _ =>
        }

        methodInfo.localVariables ++= localLookupMap.values

        def u1: Int = code({
          val index = offset
          offset += 1
          index
        }) & 0xFF
        def u2: Int = (u1 << 8) + u1
        def u4: Int = (u1 << 24) + (u1 << 16) + (u1 << 8) + u1
        def s1: Byte = u1.toByte
        def s2: Short = u2.toShort


        while (offset < codeLength) {
          val instructionOffset = offset
          val rawOpcode = u1
          import BytecodeOpcode._
          BytecodeOpcode.fromOpcode(rawOpcode) match {
            case Some(opcode) =>
              opcode match {
                case AALOAD |
                     AASTORE |
                     ACONST_NULL |
                     ALOAD_0 |
                     ALOAD_1 |
                     ALOAD_2 |
                     ALOAD_3 |
                     ARETURN |
                     ARRAYLENGTH |
                     ASTORE_0 |
                     ASTORE_1 |
                     ASTORE_2 |
                     ASTORE_3 |
                     ATHROW |
                     BALOAD |
                     BASTORE |
                     CALOAD |
                     CASTORE |
                     D2F |
                     D2I |
                     D2L |
                     DADD |
                     DALOAD |
                     DASTORE |
                     DCMPG |
                     DCMPL |
                     DCONST_0 |
                     DCONST_1 |
                     DDIV |
                     DLOAD_0 |
                     DLOAD_1 |
                     DLOAD_2 |
                     DLOAD_3 |
                     DMUL |
                     DNEG |
                     DREM |
                     DRETURN |
                     DSTORE_0 |
                     DSTORE_1 |
                     DSTORE_2 |
                     DSTORE_3 |
                     DSUB |
                     DUP |
                     DUP_X1 |
                     DUP_X2 |
                     DUP2 |
                     DUP2_X1 |
                     DUP2_X2 |
                     F2D |
                     F2I |
                     F2L |
                     FADD |
                     FALOAD |
                     FASTORE |
                     FCMPG |
                     FCMPL |
                     FCONST_0 |
                     FCONST_1 |
                     FCONST_2 |
                     FDIV |
                     FLOAD_0 |
                     FLOAD_2 |
                     FLOAD_3 |
                     FMUL |
                     FNEG |
                     FREM |
                     FRETURN |
                     FSTORE_0 |
                     FSTORE_1 |
                     FSTORE_2 |
                     FSTORE_3 |
                     FSUB |
                     I2B |
                     I2C |
                     I2D |
                     I2F |
                     I2L |
                     I2S |
                     IADD |
                     IALOAD |
                     IAND |
                     IASTORE |
                     ICONST_M1 |
                     ICONST_0 |
                     ICONST_1 |
                     ICONST_2 |
                     ICONST_3 |
                     ICONST_4 |
                     ICONST_5 |
                     IDIV |
                     ILOAD_0 |
                     ILOAD_1 |
                     ILOAD_2 |
                     ILOAD_3 |
                     IMUL |
                     INEG |
                     IOR |
                     IREM |
                     IRETURN |
                     ISHL |
                     ISHR |
                     ISTORE_0 |
                     ISTORE_1 |
                     ISTORE_2 |
                     ISTORE_3 |
                     ISUB |
                     IUSHR |
                     IXOR |
                     L2D |
                     L2F |
                     L2I |
                     LADD |
                     LALOAD |
                     LAND |
                     LASTORE |
                     LCMP |
                     LCONST_0 |
                     LCONST_1 |
                     LDIV |
                     LLOAD_0 |
                     LLOAD_1 |
                     LLOAD_2 |
                     LLOAD_3 |
                     LMUL |
                     LNEG |
                     LOR |
                     LREM |
                     LRETURN |
                     LSHL |
                     LSHR |
                     LSTORE_0 |
                     LSTORE_1 |
                     LSTORE_2 |
                     LSTORE_3 |
                     LSUB |
                     LUSHR |
                     LXOR |
                     MONITORENTER |
                     MONITOREXIT |
                     NOP |
                     POP |
                     POP2 |
                     RETURN |
                     SALOAD |
                     SASTORE |
                     SWAP => instructions += (instructionOffset -> SimpleInstruction(opcode))

                case INVOKEINTERFACE |
                     INVOKESPECIAL |
                     INVOKESTATIC |
                     INVOKEVIRTUAL |
                     GETSTATIC |
                     GETFIELD |
                     PUTFIELD |
                     PUTSTATIC =>
                  constantPool.info(u2) match {
                    case reference: ReferenceInfo => instructions += (instructionOffset -> ReferenceInstruction(opcode, reference.classInfo.value, reference.nameAndTypeInfo.name, reference.nameAndTypeInfo.descriptor))
                    case _ => throw new HippoCafeException(s"Invalid reference instruction at $offset")
                  }
                  if (opcode == INVOKEINTERFACE) u2

                case INVOKEDYNAMIC =>
                  val bsms = bootstrapMethods match {
                    case Some(value) => value
                    case None => throw new HippoCafeException("Found invoke dynamic instruction, but class has no BootstrapMethods Attribute.")
                  }
                  constantPool.info(u2) match {
                    case dynamicInfo: DynamicInfo =>
                      instructions += (instructionOffset -> InvokeDynamicInstruction(Constant.fromInfo(dynamicInfo).asInstanceOf[InvokeDynamicConstant]))
                    case _ => throw new HippoCafeException(s"Invalid invoke dynamic instruction at $offset")
                  }
                  u2

                case ALOAD |
                     ASTORE |
                     DLOAD |
                     DSTORE |
                     FLOAD |
                     FSTORE |
                     ILOAD |
                     ISTORE |
                     LLOAD |
                     LSTORE |
                     RET =>
                  instructions += (instructionOffset -> impl.VariableInstruction(opcode, u1))

                case SIPUSH | BIPUSH =>
                  val value: Int = if (opcode == SIPUSH) s2 else s1
                  instructions += (instructionOffset -> PushInstruction(value))

                case IINC => instructions += (instructionOffset -> IncrementInstruction(u1, s1))

                case WIDE =>
                  val widenedOpcodeOptional = BytecodeOpcode.fromOpcode(u1)
                  widenedOpcodeOptional match {
                    case Some(value) =>
                      if (value == IINC) {
                        instructions += (instructionOffset -> IncrementInstruction(u2, s2))
                      } else {
                        instructions += (instructionOffset -> VariableInstruction(value, u2))
                      }
                    case None => throw new HippoCafeException(s"Invalid wide instruction at $offset")
                  }

                case INSTANCEOF | NEW | CHECKCAST => instructions += (instructionOffset -> TypeInstruction(opcode, constantPool.readString(u2)))

                case GOTO |
                     GOTO_W |
                     JSR |
                     JSR_W |
                     IFNE |
                     IFEQ |
                     IFNULL |
                     IFGE |
                     IFNONNULL |
                     IFLE |
                     IFGT |
                     IFLT |
                     IF_ACMPEQ |
                     IF_ACMPNE |
                     IF_ICMPEQ |
                     IF_ICMPGE |
                     IF_ICMPGT |
                     IF_ICMPLE |
                     IF_ICMPLT |
                     IF_ICMPNE =>
                  val branchOffset: Int = if (opcode == GOTO_W || opcode == JSR_W) u4 else s2
                  val label = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  addLabel(instructionOffset + branchOffset, label)
                  instructions += (instructionOffset -> BranchInstruction(opcode, label))

                case LDC | LDC_W | LDC2_W =>
                  instructions += (instructionOffset -> ConstantInstruction(Constant.fromInfo(constantPool.info(if (opcode == LDC) u1 else u2))))

                case LOOKUPSWITCH =>
                  offset += -offset & 3
                  val default = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  addLabel(instructionOffset + u4, default)
                  val lookupSwitch = LookupSwitchInstruction(default)
                  val cases = u4
                  (0 until cases).foreach(_ => {
                    val key = u4
                    val branch = LabelInstruction(labelDebugId)
                    labelDebugId += 1
                    addLabel(instructionOffset + u4, branch)
                    lookupSwitch.pairs += (key -> branch)
                  })
                  instructions += (instructionOffset -> lookupSwitch)

                case TABLESWITCH =>
                  offset += -offset & 3
                  val default = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  addLabel(instructionOffset + u4, default)
                  val low = u4
                  val high = u4
                  val cases = high - low + 1
                  val tableSwitch = TableSwitchInstruction(default, low, high)
                  (0 until cases).foreach(_ => {
                    val branch = LabelInstruction(labelDebugId)
                    labelDebugId += 1
                    addLabel(instructionOffset + u4, branch)
                    tableSwitch.table += branch
                  })
                  instructions += (instructionOffset -> tableSwitch)

                case ANEWARRAY =>
                  instructions += (instructionOffset -> ANewArrayInstruction(constantPool.readString(u2)))
                case NEWARRAY =>
                  val typeId = u1
                  ArrayType.fromType(typeId) match {
                    case Some(value) =>
                      instructions += (instructionOffset -> NewArrayInstruction(value))
                    case None =>
                      throw new HippoCafeException(s"Could not find array type for $typeId")
                  }
                case MULTIANEWARRAY =>
                  instructions += (instructionOffset -> MultiANewArrayInstruction(constantPool.readString(u2), u1))
              }
            case None => throw new HippoCafeException(s"Could not find opcode for $rawOpcode")
          }
        }


        exceptions.foreach(exception => {
          val start = LabelInstruction(labelDebugId)
          labelDebugId += 1
          val end = LabelInstruction(labelDebugId)
          labelDebugId += 1
          val handler = LabelInstruction(labelDebugId)
          labelDebugId += 1


          addLabel(exception.startPc, start)
          addLabel(exception.endPc, end)
          addLabel(exception.handlerPc, handler)
          tryCatchBlocks += TryCatchBlock(start, end, handler, exception.catchType)
        })

        def updateUninitializedVerificationInfo(verificationTypeInfo: VerificationTypeInfo): Unit = {
          verificationTypeInfo match {
            case uninitializedVerificationTypeInfo: UninitializedVerificationTypeInfo =>
              uninitializedVerificationTypeInfo.typeInstruction = Option(instructions(uninitializedVerificationTypeInfo.offset).asInstanceOf[TypeInstruction])
            case _ =>
          }
        }

        labels.foreach(_._2.foreach {
          case SameLocalsFrameInstruction(verificationTypeInfo) =>
            updateUninitializedVerificationInfo(verificationTypeInfo)
          case AppendFrameInstruction(locals) =>
            locals.foreach(updateUninitializedVerificationInfo)
          case FullFrameInstruction(locals, stack) =>
            locals.foreach(updateUninitializedVerificationInfo)
            stack.foreach(updateUninitializedVerificationInfo)
          case _ =>
        })

        var sortedDebug = 0
        labels.values.foreach(_.reverse.foreach {
          case labelInstruction: LabelInstruction =>
            labelInstruction.debugId = sortedDebug
            sortedDebug += 1
          case _ =>
        })

        var push = 0
        labels.foreach(entry => {
          val offset = entry._1 + push
          val labels = entry._2
          labels.foreach(label => {
            if (instructions.contains(offset)) {
              val newInstructions = mutable.SortedMap[Int, Instruction]()
              var copy = 0
              instructions.foreach(entry => {
                if (entry._1 == offset) {
                  copy += 1
                  push += 1
                  newInstructions += (entry._1 -> label)
                }
                newInstructions += (entry._1 + copy -> entry._2)
              })
              instructions.clear()
              instructions ++= newInstructions
            } else {
              instructions += (offset -> label)
            }
          })
        })

        methodInfo.instructions ++= instructions.values.toList
        methodInfo.tryCatchBlocks ++= tryCatchBlocks.result()
        methodInfo.maxLocals = codeAttribute.maxLocals
        methodInfo.maxStack = codeAttribute.maxStack
        methodInfo.attributes -= codeAttribute
      case _ =>

    }

  }
}
