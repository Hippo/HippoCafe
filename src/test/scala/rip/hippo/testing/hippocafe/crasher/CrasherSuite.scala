package rip.hippo.testing.hippocafe.crasher

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.{ClassReader, ClassWriter}

import java.io.{File, FileInputStream}
import scala.collection.mutable.ListBuffer
import scala.util.{Failure, Success, Using}

/**
 * @author Hippo
 * @version 1.0.0, 12/4/21
 * @since 2.0.0
 */
final class CrasherSuite extends AnyFunSuite {

  test("crashers") {
    val crashersDirectory = new File(Thread.currentThread().getContextClassLoader.getResource("crasher").getFile)
    crashersDirectory.listFiles().filter(_.getName.endsWith(".class")).foreach(crasher => {
      Using(new ClassReader(new FileInputStream(crasher))) {
        classReader =>
          classReader.classFile
      } match {
        case Failure(exception) =>
          println("Failed to read -> " + crasher.getName)
            exception.printStackTrace()
        case Success(value) =>
          println(s"Successfully read -> ${value.name} (${crasher.getName}), trying to write..." )
          Using(new ClassWriter(value)) {
            classWriter =>
              classWriter.write
              value
          } match {
            case Failure(exception) =>
              println("Failed to write -> " + value.name)
              exception.printStackTrace()
            case Success(value) =>
              println("Successfully wrote -> " + value.name)
          }
      }
    })

  }

}
