package rip.hippo.api.hippocafe.util

import rip.hippo.api.hippocafe.util.Type.{DOUBLE, LONG}

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
object Type {
  private implicit def newType(descriptor: String): Type = Type(descriptor)

  val VOID: Type = "V"
  val BOOLEAN: Type = "Z"
  val BYTE: Type = "B"
  val CHAR: Type = "C"
  val SHORT: Type = "S"
  val INT: Type = "I"
  val FLOAT: Type = "F"
  val LONG: Type = "J"
  val DOUBLE: Type = "D"
  def OBJECT(descriptor: String): Type = descriptor


  def getType(descriptor: String): Type =
    descriptor.charAt(0) match {
      case 'V' => VOID
      case 'Z' => BOOLEAN
      case 'B' => BYTE
      case 'C' => CHAR
      case 'S' => SHORT
      case 'I' => INT
      case 'F' => FLOAT
      case 'J' => LONG
      case 'D' => DOUBLE
      case _ => OBJECT(descriptor)
    }
}

sealed case class Type(descriptor: String) {
  def isWide: Boolean = this == LONG || this == DOUBLE
  def isArray: Boolean = descriptor.charAt(0) == '['
}