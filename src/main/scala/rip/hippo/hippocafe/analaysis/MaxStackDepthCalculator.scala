package rip.hippo.hippocafe.analaysis

import rip.hippo.hippocafe.MethodInfo

/**
 * @author Hippo
 * @version 1.0.0, 2/22/21
 * @since 1.0.0
 */
trait MaxStackDepthCalculator {
  def calculateMaxStack(methodInfo: MethodInfo): Int
}
