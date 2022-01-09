package rip.hippo.hippocafe.attribute.impl

import rip.hippo.hippocafe.attribute.Attribute.{Attribute, PERMITTED_SUBCLASSES}
import rip.hippo.hippocafe.attribute.AttributeInfo
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}

import java.io.DataOutputStream
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.0.0, 12/7/21
 * @since 2.2.0
 */
final case class PermittedSubclassesAttribute(classes: ListBuffer[String]) extends AttributeInfo {
  override val kind: Attribute = PERMITTED_SUBCLASSES

  override def write(out: DataOutputStream, constantPool: ConstantPool): Unit = {
    out.writeShort(classes.size)
    classes.foreach(className => {
      out.writeShort(constantPool.findString(className, ConstantPoolKind.CLASS))
    })
  }

  override def buildConstantPool(constantPool: ConstantPool): Unit =
    classes.foreach(className => {
      constantPool.insertStringIfAbsent(className, ConstantPoolKind.CLASS)
    })
}
