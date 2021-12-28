/*
 * MIT License
 *
 * Copyright (c) 2021 Hippo
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

package rip.hippo.hippocafe.version

/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
enum MajorClassFileVersion(val version: Int) {
  case JDK1_1 extends MajorClassFileVersion(45)
  case JDK1_2 extends MajorClassFileVersion(46)
  case JDK1_3 extends MajorClassFileVersion(47)
  case JDK1_4 extends MajorClassFileVersion(48)
  case SE5_0 extends MajorClassFileVersion(49)
  case SE6 extends MajorClassFileVersion(50)
  case SE7 extends MajorClassFileVersion(51)
  case SE8 extends MajorClassFileVersion(52)
  case SE9 extends MajorClassFileVersion(53)
  case SE10 extends MajorClassFileVersion(54)
  case SE11 extends MajorClassFileVersion(55)
  case SE12 extends MajorClassFileVersion(56)
  case SE13 extends MajorClassFileVersion(57)
  case SE14 extends MajorClassFileVersion(58)
  case SE15 extends MajorClassFileVersion(59)
  case SE16 extends MajorClassFileVersion(60)
  case SE17 extends MajorClassFileVersion(61)
  case SE18 extends MajorClassFileVersion(62)
  case SE19 extends MajorClassFileVersion(63)
}

object MajorClassFileVersion {
  def fromVersion(version: Int): Option[MajorClassFileVersion] =
    MajorClassFileVersion.values.find(_.version == version)
}
