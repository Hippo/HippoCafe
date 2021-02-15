package rip.hippo.hippocafe.constantpool.info.impl.data

import ReferenceKind.lookup

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
object ReferenceKind {
  
  private val lookup = new Array[ReferenceKind](10)
  
  val REF_GET_FIELD: ReferenceKind = ReferenceKind(1, "REF_getField")
  val REF_GET_STATIC: ReferenceKind = ReferenceKind(2, "REF_getStatic")
  val REF_PUT_FIELD: ReferenceKind = ReferenceKind(3, "REF_putField")
  val REF_PUT_STATIC: ReferenceKind = ReferenceKind(4, "REF_putStatic")
  val REF_INVOKE_VIRTUAL: ReferenceKind = ReferenceKind(5, "REF_invokeVirtual")
  val REF_INVOKE_STATIC: ReferenceKind = ReferenceKind(6, "REF_invokeStatic")
  val REF_INVOKE_SPECIAL: ReferenceKind = ReferenceKind(7, "REF_invokeSpecial")
  val REF_NEW_INVOKE_SPECIAL: ReferenceKind = ReferenceKind(8, "REF_newInvokeSpecial")
  val REF_INVOKE_INTERFACE: ReferenceKind = ReferenceKind(9, "REF_invokeInterface")

  def valueOf(index: Int): ReferenceKind = lookup(index)
}

sealed case class ReferenceKind(kind: Int, description: String) {
  lookup(kind) = this
}
