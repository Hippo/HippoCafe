/*
 * MIT License
 *
 * Copyright (c) 2020 Hippo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

package rip.hippo.api.hippocafe.version

/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
object MajorClassFileVersion extends Enumeration {
  implicit private def toValue(version: Int): Value = Value(version)

  type MajorClassFileVersion = Value
  
  val JDK1_1: MajorClassFileVersion = 45
  val JDK1_2: MajorClassFileVersion = 46
  val JDK1_3: MajorClassFileVersion = 47
  val JDK1_4: MajorClassFileVersion = 48
  val SE5_0: MajorClassFileVersion = 49
  val SE6: MajorClassFileVersion = 50
  val SE7: MajorClassFileVersion = 51
  val SE8: MajorClassFileVersion = 52
  val SE9: MajorClassFileVersion = 53
  val SE10: MajorClassFileVersion = 54
  val SE11: MajorClassFileVersion = 55
  val SE12: MajorClassFileVersion = 56
  val SE13: MajorClassFileVersion = 57
  val SE14: MajorClassFileVersion = 58
  val JDK15: MajorClassFileVersion = 59
  val JDK16: MajorClassFileVersion = 60

  def fromVersion(version: Int): Option[MajorClassFileVersion] =
    MajorClassFileVersion.values.find(_.id == version)
}
