package rip.hippo.testing.hippocafe.crasher

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.ClassReader

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
    val crashersDirectory = new File(Thread.currentThread().getContextClassLoader.getResource("crasher/sub").getFile)
    crashersDirectory.listFiles().filter(_.getName.endsWith(".class")).foreach(crasher => {
      val test = Using(new ClassReader(new FileInputStream(crasher))) {
        classReader =>
          classReader.classFile
      }
      test match {
        case Failure(exception) =>
          println("Failed to read -> " + crasher.getName)
            exception.printStackTrace()
        case Success(value) =>
          value.attributes.foreach(println)
          println()
          value.fields.foreach(_.attributes.foreach(println))
          println()
          value.methods.foreach(_.attributes.foreach(println))
          println(s"Successfully read -> ${value.name} (${crasher.getName})" )
      }
    })

  }

}
