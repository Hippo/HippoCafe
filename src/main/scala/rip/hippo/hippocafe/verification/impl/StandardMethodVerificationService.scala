package rip.hippo.hippocafe.verification.impl

import rip.hippo.hippocafe.{ClassFile, MethodInfo}
import rip.hippo.hippocafe.access.AccessFlag
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode.*
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.{DoubleConstant, LongConstant}
import rip.hippo.hippocafe.disassembler.instruction.impl.*
import rip.hippo.hippocafe.disassembler.instruction.{FrameInstruction, Instruction}
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.stackmap.verification.{VerificationTypeInfo, impl}
import rip.hippo.hippocafe.stackmap.verification.impl.*
import rip.hippo.hippocafe.util.Type
import rip.hippo.hippocafe.util.Type.*
import rip.hippo.hippocafe.verification.MethodVerificationService
import rip.hippo.hippocafe.verification.block.{Block, BlockService}
import rip.hippo.hippocafe.verification.cfg.{BlockNode, ControlFlowGraphService}
import rip.hippo.hippocafe.verification.frame.VirtualFrame

import scala.collection.mutable
import scala.collection.mutable.{ArrayBuffer, ListBuffer}

/**
 * @author Hippo
 * @version 1.1.0, 2/22/21
 * @since 1.0.0
 */
final class StandardMethodVerificationService extends MethodVerificationService {


  override def calculateMaxStack(classFile: ClassFile, methodInfo: MethodInfo): Int =
    BlockService(classFile, methodInfo).blocks.map(_.getBlockDepth._1).max

  override def generateFrames(classFile: ClassFile, methodInfo: MethodInfo): Unit = {
    // remove existing frames
    methodInfo.instructions.filter(_.isInstanceOf[FrameInstruction]).foreach(methodInfo.instructions.-=)

    // generate a unique id for all instructions
    methodInfo.instructions.foreach(_.getUniqueId)


    val service = ControlFlowGraphService(classFile, methodInfo)

    if (service.blockNodes.size > 1) {


      def newFrame: VirtualFrame = {
        val locals = mutable.Map[Int, VerificationTypeInfo]()

        var index = 0
        if (!methodInfo.accessFlags.contains(AccessFlag.ACC_STATIC)) {
          val info = if (methodInfo.name.equals("<init>")) UninitializedThisVerificationTypeInfo() else ObjectVerificationTypeInfo(classFile.name)
          locals += (index -> info)
          index += 1
        }
        Type.getMethodParameterTypes(methodInfo.descriptor).foreach {
          case BOOLEAN | BYTE | CHAR | SHORT | INT =>
            locals += (index -> IntegerVerificationTypeInfo())
            index += 1
          case FLOAT =>
            locals += (index -> FloatVerificationTypeInfo())
            index += 1
          case LONG =>
            locals += (index -> LongVerificationTypeInfo())
            locals += (index + 1 -> TopVerificationTypeInfo())
            index += 2
          case DOUBLE =>
            locals += (index -> DoubleVerificationTypeInfo())
            locals += (index + 1 -> TopVerificationTypeInfo())
            index += 2
          case x =>
            locals += (index -> ObjectVerificationTypeInfo(x.descriptor))
            index += 1
        }

        VirtualFrame(locals, this)
      }

      var lastFrame = newFrame

      service.blockNodes.filter(_.block.requiresFrame()).foreach(blockNode => {
        val block = blockNode.block
        val label = block.getStartingLabel.get
        val frames = ListBuffer[VirtualFrame]()
        val copiedList = blockNode.previousBlocks.toList

        (0 until blockNode.previousBlocks.size).foreach(i => {
          val jumpTargets = ListBuffer[BlockNode]()

          var current = blockNode


          var flag = true
          while (current.previousBlocks.nonEmpty) {
            val node = if (flag) copiedList(i) else current.previousBlocks.head
            flag = false
            jumpTargets += node
            current = node
          }



          val frame = newFrame
          jumpTargets.reverse.foreach(_.block.instructions.foreach(instruction => frame.execute(classFile, methodInfo, instruction)))

          val tcbTypes = methodInfo.tryCatchBlocks.filter(_.handler == label).map(_.catchType)
          if (tcbTypes.nonEmpty) {
            frame.stack.clear()
            val commonType = tcbTypes.foldLeft(tcbTypes.head)((t1, t2) => getCommonSuperType(t1, t2))
            frame.push(ObjectVerificationTypeInfo(commonType))
          }

          frames += frame
        })

        val frame = frames.foldLeft(frames.head)((f1, f2) => f1.merge(f2))


        val idMirror = methodInfo.instructions.map(_.getUniqueId)
        val index = idMirror.indexOf(label.getUniqueId)
        var inserted = false


        if (lastFrame.equals(frame)) {
          methodInfo.instructions.insert(index + 1, SameFrameInstruction())
          inserted = true
        } else {
          val sameStack = lastFrame.sameStack(frame)
          val sameLocals = lastFrame.sameLocals(frame)

          if (sameStack && !sameLocals) {
            val lastLocals = lastFrame.getActualLocals.values.toList
            val currentLocals = frame.getActualLocals.values.toList

            val minLength = Math.min(lastLocals.size, currentLocals.size)
            val maxLength = Math.max(lastLocals.size, currentLocals.size)
            if (minLength != maxLength) {
              if (minLength != maxLength) {
                var equalLocal = true
                (0 until minLength).foreach(i => {
                  if (lastLocals(i) != currentLocals(i)) {
                    equalLocal = false
                  }
                })

                if (equalLocal) {
                  val append = maxLength == currentLocals.size
                  if (append) {
                    val buffer = ArrayBuffer[VerificationTypeInfo]()
                    (minLength until maxLength).foreach(i => buffer += currentLocals(i))
                    methodInfo.instructions.insert(index + 1, AppendFrameInstruction(buffer.toArray))
                    inserted = true
                  } else {
                    methodInfo.instructions.insert(index + 1, ChopFrameInstruction(maxLength - minLength))
                    inserted = true
                  }
                }
              }
            }
          } else if (!sameStack && sameLocals) {
            if (frame.getActualStack.size == 1) {
              methodInfo.instructions.insert(index + 1, SameLocalsFrameInstruction(frame.getActualStack.last))
              inserted = true
            }
          }


          if (!inserted) {
            methodInfo.instructions.insert(index + 1, FullFrameInstruction(frame.getActualLocals.values.toArray, frame.getActualStack.toArray))
          }

          lastFrame = frame
        }
      })
    }
  }

  override def getCommonSuperType(type1: String, type2: String): String = {
    val classLoader = getClassLoader
    var class1 = Class.forName(type1.replace('/', '.'), false, classLoader)
    val class2 = Class.forName(type2.replace('/', '.'), false, classLoader)
    if (class1.isAssignableFrom(class2)) return type1
    if (class2.isAssignableFrom(class1)) return type2
    if (class1.isInterface || class2.isInterface) return "java/lang/Object"
    else while {
      class1 = class1.getSuperclass
      !class1.isAssignableFrom(class2)
    } do ()
    class1.getName.replace('.', '/')
  }

  override def getClassLoader: ClassLoader =
    getClass.getClassLoader
}
