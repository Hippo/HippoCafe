package rip.hippo.hippocafe.attribute.impl.data.annotation.value.impl.data

/**
 * @author Hippo
 * @version 1.0.0, 12/8/21
 * @since 2.0.0
 */
enum ConstantAnnotationValueType(val tag: Int) {
  case BYTE extends ConstantAnnotationValueType('B'.toInt)
  case CHAR extends ConstantAnnotationValueType('C'.toInt)
  case DOUBLE extends ConstantAnnotationValueType('D'.toInt)
  case FLOAT extends ConstantAnnotationValueType('F'.toInt)
  case INT extends ConstantAnnotationValueType('I'.toInt)
  case LONG extends ConstantAnnotationValueType('J'.toInt)
  case SHORT extends ConstantAnnotationValueType('S'.toInt)
  case BOOLEAN extends ConstantAnnotationValueType('Z'.toInt)
  case STRING extends ConstantAnnotationValueType('s'.toInt)
}

object ConstantAnnotationValueType {
  def fromTag(tag: Int): Option[ConstantAnnotationValueType] =
    ConstantAnnotationValueType.values.find(_.tag == tag)
}
