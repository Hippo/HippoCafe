package rip.hippo.hippocafe.disassembler.context

import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 2/19/21
 * @since 1.0.0
 */
class AssemblerContext {
  val code: ListBuffer[Byte] = ListBuffer[Byte]()
  var maxStack = 0
  var maxLocals = 0
}
