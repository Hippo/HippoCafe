package rip.hippo.testing.hippocafe

import org.scalatest.FunSuite
import rip.hippo.hippocafe.ClassReader

import scala.util.Using

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
        Using(new ClassReader(value, true)) {
          classReader =>
            val classFile = classReader.classFile
            classFile.constantPool match {
              case Some(value) =>
                value.info.foreach(println)
              case None =>
                println("No constant pool")
            }
        }
        value.close()
      case None => println(s"Could not load resource $className.class")
    }
  }
}
