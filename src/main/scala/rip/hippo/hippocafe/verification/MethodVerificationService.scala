package rip.hippo.hippocafe.verification

import rip.hippo.hippocafe.ClassFile
import rip.hippo.hippocafe.MethodInfo

/**
 * @author Hippo
 * @version 1.0.0, 2/22/21
 * @since 1.0.0
 */
trait MethodVerificationService:
  def calculateMaxStack(classFile: ClassFile, methodInfo: MethodInfo): Int
  def generateFrames(classFile: ClassFile, methodInfo: MethodInfo): Unit
  def getCommonSuperType(type1: String, type2: String): String
  def getClassLoader: ClassLoader