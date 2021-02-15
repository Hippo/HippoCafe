package rip.hippo.testing.hippocafe

import org.scalatest.FunSuite
import rip.hippo.hippocafe.ClassReader

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
final class ConstantPoolSuite extends FunSuite {
  private val className = "HelloWorld"

  test("ClassReader.read.cp") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val classReader = new ClassReader(value, true)
        val classFile = classReader.classFile
        classFile.constantPool match {
          case Some(value) =>
            value.info.foreach(println)
          case None =>
            println("No constant pool")
        }
      case None => println(s"Could not load resource $className.class")
    }
  }
}
