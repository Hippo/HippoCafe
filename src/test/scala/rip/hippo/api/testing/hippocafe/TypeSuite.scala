package rip.hippo.api.testing.hippocafe

import org.scalatest.FunSuite
import rip.hippo.api.hippocafe.util.Type

/**
 * @author Hippo
 * @version 1.0.0, 2/13/21
 * @since 1.0.0
 */
final class TypeSuite extends FunSuite {

  test("Type.methodParams") {
    val descriptor = "(Ljava/lang/String;[IIBZLjava/lang/Object;)V"
    Type.getMethodParameterTypes(descriptor).foreach(println)
  }
}
