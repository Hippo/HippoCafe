package rip.hippo.testing.hippocafe

import org.scalatest.funsuite.AnyFunSuite
import rip.hippo.hippocafe.util.Type

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
final class TypeSuite extends AnyFunSuite {

  test("Type.methodParams") {
    val descriptor = "(Ljava/lang/String;[IIBZLjava/lang/Object;[Ljava/lang/String;[[Ljava/lang/String;)V"
    Type.getMethodParameterTypes(descriptor).foreach(println)
    println(Type.getMethodReturnType(descriptor))
  }
}
