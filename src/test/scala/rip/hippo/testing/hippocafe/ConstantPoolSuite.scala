package rip.hippo.testing.hippocafe

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.ClassReader

import scala.util.{Failure, Using}

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
final class ConstantPoolSuite extends AnyFunSuite {
  private val className = "HelloWorld"

  test("ClassReader.read.cp") {
    Option(Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")) match {
      case Some(value) =>
        val test = Using(new ClassReader(value, true)) {
          classReader =>
            val classFile = classReader.classFile
            classFile.constantPool match {
              case Some(value) =>
                value.info.foreach(println)
              case None =>
                println("No constant pool")
            }
        }
        test match {
          case Failure(exception) => throw exception
          case _ =>
        }
        value.close()
      case None => println(s"Could not load resource $className.class")
    }
  }
}
