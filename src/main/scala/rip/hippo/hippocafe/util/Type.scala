package rip.hippo.hippocafe.util

import Type.{DOUBLE, LONG, VOID}

import scala.collection.mutable.ListBuffer
import scala.language.implicitConversions

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

  def getMethodParameterTypes(descriptor: String): ListBuffer[Type] = {
    val types = ListBuffer[Type]()
    var index = 1
    while (descriptor.charAt(index) != ')') {
      val start = index
      while (descriptor.charAt(index) == '[') index += 1
      var end = index + 1
      if (descriptor.charAt(index) == 'L') {
        end = descriptor.indexOf(';', index) + 1
      }
      types += getType(descriptor.substring(start, end))
      index = end
    }
    types
  }

  def getMethodReturnType(descriptor: String): Type =
    Type.getType(descriptor.substring(descriptor.lastIndexOf(')') + 1))
  
  def getArrayType(descriptor: String): String =
    if (descriptor.startsWith("[")) getArrayType(descriptor.substring(1)) else descriptor
}

sealed case class Type(descriptor: String) {
  def isWide: Boolean = this == LONG || this == DOUBLE
  def isArray: Boolean = descriptor.charAt(0) == '['
  def isObject: Boolean = isArray || descriptor.charAt(0) == 'L'
  def getSize: Int = if (isWide) 2 else if (this == VOID) 0 else 1
  def getInternalName: String = if (descriptor.charAt(0) != 'L') descriptor else descriptor.substring(1, descriptor.length - 1)
}