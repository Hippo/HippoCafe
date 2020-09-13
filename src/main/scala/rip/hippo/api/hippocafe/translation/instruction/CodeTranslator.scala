/*
 * MIT License
 *
 * Copyright (c) 2020 Hippo
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

package rip.hippo.api.hippocafe.translation.instruction

import rip.hippo.api.hippocafe.MethodInfo
import rip.hippo.api.hippocafe.attribute.Attribute
import rip.hippo.api.hippocafe.attribute.impl.CodeAttribute
import rip.hippo.api.hippocafe.constantpool.ConstantPool
import rip.hippo.api.hippocafe.constantpool.info.ValueAwareness
import rip.hippo.api.hippocafe.constantpool.info.impl.{ReferenceInfo, StringInfo}
import rip.hippo.api.hippocafe.exception.HippoCafeException
import rip.hippo.api.hippocafe.translation.instruction.impl.{BranchInstruction, ConstantInstruction, IncrementInstruction, LabelInstruction, LookupSwitchInstruction, PushInstruction, ReferenceInstruction, SimpleInstruction, TableSwitchInstruction, TypeInstruction, VariableInstruction}
import rip.hippo.api.hippocafe.translation.instruction.impl.PushInstruction
import rip.hippo.api.hippocafe.translation.tcb.TryCatchBlock

import scala.collection.mutable
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
object CodeTranslator {


  def translate(methodInfo: MethodInfo, constantPool: ConstantPool): Unit = {

    methodInfo.attributes.find(_.kind == Attribute.CODE) match {
      case Some(codeAttribute: CodeAttribute) =>
        val instructions = mutable.SortedMap[Int, Instruction]()
        val tryCatchBlocks = ListBuffer[TryCatchBlock]()
        var offset = 0
        val code = codeAttribute.code
        val exceptions = codeAttribute.exceptionTable
        val codeLength = code.length
        val labels = mutable.SortedMap[Int, LabelInstruction]()
        var labelDebugId = 0

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
                     I2l |
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
                  if (opcode == INVOKEINTERFACE) offset += 2

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
                  instructions += (instructionOffset -> VariableInstruction(opcode, u1))

                case SIPUSH | BIPUSH =>
                  val value = if (opcode == SIPUSH) s2 else s1
                  instructions += (instructionOffset -> PushInstruction(value))

                case IINC => instructions += (instructionOffset -> IncrementInstruction(u1, s1))

                case WIDE =>
                  val widenedOpcodeOptional = BytecodeOpcode.fromOpcode(u1)
                  widenedOpcodeOptional match {
                    case Some(value) =>
                      if (value == IINC) {
                        instructions += (instructionOffset -> IncrementInstruction(u2, s2))
                      } else {
                        instructions += (instructionOffset -> impl.VariableInstruction(value, u2))
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
                  val branchOffset = if (opcode == GOTO_W || opcode == JSR_W) u4 else s2
                  val label = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  labels += (instructionOffset + branchOffset -> label)
                  instructions += (instructionOffset -> BranchInstruction(opcode, label))

                case LDC | LDC_W | LDC2_W =>
                  constantPool.info(if (opcode == LDC) u1 else u2) match {
                    case aware: ValueAwareness[_] => instructions += (instructionOffset -> ConstantInstruction(aware.value))
                    case stringInfo: StringInfo => instructions += (instructionOffset -> ConstantInstruction(stringInfo.value))
                    case _ => throw new HippoCafeException(s"Invalid constant instruction at $offset")
                  }

                case LOOKUPSWITCH =>
                  offset += -offset & 3
                  val default = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  labels += (instructionOffset + u4 -> default)
                  val lookupSwitch = LookupSwitchInstruction(default)
                  val cases = u4
                  (0 until cases).foreach(_ => {
                    val key = u4
                    val branch = LabelInstruction(labelDebugId)
                    labelDebugId += 1
                    labels += (instructionOffset + u4 -> branch)
                    lookupSwitch.pairs += (key -> branch)
                  })
                  instructions += (instructionOffset -> lookupSwitch)

                case TABLESWITCH =>
                  offset += -offset & 3
                  val default = LabelInstruction(labelDebugId)
                  labelDebugId += 1
                  labels += (instructionOffset + u4 -> default)
                  val low = u4
                  val high = u4
                  val cases = high - low + 1
                  val tableSwitch = TableSwitchInstruction(default, low, high)
                  (0 until cases).foreach(_ => {
                    val branch = LabelInstruction(labelDebugId)
                    labelDebugId += 1
                    labels += (instructionOffset + u4 -> branch)
                    tableSwitch.table += branch
                  })
                  instructions += (instructionOffset -> tableSwitch)

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

          println(exception.handlerPc + " " + exception.endPc)
          println(instructions)

          labels += (exception.startPc -> start)
          labels += (exception.endPc -> end)
          labels += (exception.handlerPc -> handler)
          tryCatchBlocks += TryCatchBlock(start, end, handler, constantPool.readString(exception.catchType))
        })

        labels.foreach(entry => {
          val offset = entry._1
          val label = entry._2
          if (instructions.contains(offset)) {
            var copy = 0
            val newInstructions = mutable.SortedMap[Int, Instruction]()
            instructions.foreach(entry => {
              if (entry._1 == offset) {
                copy += 1
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
       // println(instructions)
        methodInfo.instructions ++= instructions.values.toList
        methodInfo.tryCatchBlocks ++= tryCatchBlocks.result()
        methodInfo.attributes -= codeAttribute
      case _ =>

    }

  }
}
