package rip.hippo.testing.hippocafe.assemble

import org.scalatest.FunSuite
import rip.hippo.hippocafe.ClassWriter
import rip.hippo.hippocafe.access.AccessFlag.{ACC_PUBLIC, ACC_STATIC}
import rip.hippo.hippocafe.builder.ClassBuilder
import rip.hippo.hippocafe.disassembler.instruction.BytecodeOpcode._
import rip.hippo.hippocafe.disassembler.instruction.Instruction
import rip.hippo.hippocafe.disassembler.instruction.impl._
import rip.hippo.hippocafe.version.MajorClassFileVersion.SE8
import rip.hippo.testing.hippocafe.CustomClassLoader

import scala.collection.mutable
import scala.util.Using


/**
 * @author Hippo
 * @version 1.0.0, 2/21/21
 * @since 1.0.0
 */
final class VariableAssemblerSuite extends FunSuite {

  test("assemble.variable") {
    val classFile = ClassBuilder(
      SE8,
      "Variable",
      "java/lang/Object",
      ACC_PUBLIC
    ).method(
      "<init>",
      "()V",
      ACC_PUBLIC
    ).maxStack(1).apply(instructions => {
      instructions += SimpleInstruction(ALOAD_0)
      instructions += ReferenceInstruction(INVOKESPECIAL, "java/lang/Object", "<init>", "()V")
      instructions += SimpleInstruction(RETURN)
    }).method(
      "main",
      "([Ljava/lang/String;)V",
      ACC_PUBLIC, ACC_STATIC
    ).maxStack(2).apply(instructions => {
      instructions += PushInstruction(69)
      instructions += SimpleInstruction(ISTORE_1)
      instructions += ReferenceInstruction(GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;")
      instructions += SimpleInstruction(ILOAD_1)
      instructions += ReferenceInstruction(INVOKEVIRTUAL, "java/io/PrintStream", "println", "(I)V")
      instructions += SimpleInstruction(RETURN)
    }).result
    Using(new ClassWriter(classFile).calculateMaxes) {
      classWriter =>
        val bytecode = classWriter.write
        val classLoader = new CustomClassLoader()
        val dynamicClass = classLoader.createClass("Variable", bytecode)
        dynamicClass.getMethod("main", classOf[Array[String]]).invoke(null, null)
    }
  }

}
