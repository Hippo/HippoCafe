package rip.hippo.hippocafe.disassembler.context

/**
 * @author Hippo
 * @version 1.0.0, 3/7/21
 * @since 1.0.1
 */
final class UniqueByte(var byte: Byte) {
  override def toString: String = s"UniqueByte($byte)"
}

object UniqueByte {
  def apply(byte: Byte): UniqueByte = new UniqueByte(byte)
}
