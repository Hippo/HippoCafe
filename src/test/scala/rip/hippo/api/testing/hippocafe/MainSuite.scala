package rip.hippo.api.testing.hippocafe

import org.scalatest.FunSuite
import rip.hippo.api.hippocafe.{ClassReader, ClassWriter}

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.1.0
 */
final class MainSuite extends FunSuite {

  private val className = "SwitchTest"


  sealed class CustomClassLoader extends ClassLoader {
    def createClass(name: String, bytecode: Array[Byte]): Class[_] = super.defineClass(name, bytecode, 0, bytecode.length)
  }

  test("ClassReader.read") {
    val inputStream = Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")
    val classReader = new ClassReader(inputStream)
    val classFile = classReader.classFile
    println(classFile.name)

  }

  test("ClassWriter.write") {
    val inputStream = Thread.currentThread.getContextClassLoader.getResourceAsStream(s"$className.class")
    val classReader = new ClassReader(inputStream)
    val bytecode = new ClassWriter(classReader.classFile).write

    val loaded = new CustomClassLoader().createClass(className, bytecode)

    loaded.getDeclaredMethod("main", classOf[Array[String]]).invoke(null, null)

  }

}
