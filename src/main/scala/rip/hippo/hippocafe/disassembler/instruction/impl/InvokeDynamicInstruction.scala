package rip.hippo.hippocafe.disassembler.instruction.impl

import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.disassembler.context.{AssemblerContext, UniqueByte}
import rip.hippo.hippocafe.disassembler.instruction.{BytecodeOpcode, Instruction}
import rip.hippo.hippocafe.disassembler.instruction.constant.impl.InvokeDynamicConstant

/**
 * @author Hippo
 * @version 1.0.0, 6/18/21
 * @since 1.5.0
 */
final case class InvokeDynamicInstruction(invokeDynamicConstant: InvokeDynamicConstant) extends Instruction {

  override def assemble(assemblerContext: AssemblerContext, constantPool: ConstantPool): Unit = {
    invokeDynamicConstant.insertIfAbsent(constantPool)
    val code = assemblerContext.code
    val index = invokeDynamicConstant.getConstantPoolIndex(constantPool)

    val uniqueByte = UniqueByte(BytecodeOpcode.INVOKEDYNAMIC.id.toByte)
    assemblerContext.labelQueue.foreach(label => assemblerContext.labelToByte += (label -> uniqueByte))
    assemblerContext.labelQueue.clear()

    code += uniqueByte
    code += UniqueByte(((index >>> 8) & 0xFF).toByte)
    code += UniqueByte((index & 0xFF).toByte)
    code += UniqueByte(0)
    code += UniqueByte(0)
  }
}
