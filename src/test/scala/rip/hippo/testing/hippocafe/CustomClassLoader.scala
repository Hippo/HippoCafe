package rip.hippo.testing.hippocafe

/**
 * @author Hippo
 * @version 1.0.0, 2/6/21
 * @since 1.0.0
 */
final class CustomClassLoader extends ClassLoader:
  def createClass(name: String, bytecode: Array[Byte]): Class[?] = super.defineClass(name, bytecode, 0, bytecode.length)