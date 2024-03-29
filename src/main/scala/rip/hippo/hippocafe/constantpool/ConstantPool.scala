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

package rip.hippo.hippocafe.constantpool

import rip.hippo.hippocafe.constantpool.ConstantPoolKind
import rip.hippo.hippocafe.constantpool.info.impl.UTF8Info
import rip.hippo.hippocafe.constantpool.info.ConstantPoolInfo
import rip.hippo.hippocafe.constantpool.info.impl.{StringInfo, UTF8Info}

import scala.collection.mutable

/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
final class ConstantPool {
  val info: mutable.Map[Int, ConstantPoolInfo] = mutable.Map[Int, ConstantPoolInfo]()
  private var last: Option[ConstantPoolInfo] = None

  def insert(index: Int, constantPoolInfo: ConstantPoolInfo): Unit = {
    info += (index -> constantPoolInfo)
    last = Option(constantPoolInfo)
  }


  def findUTF8(value: String): Int = {
    info.filter(entry => entry._2 match {
      case utf8: UTF8Info => utf8.value.equals(value)
      case _ => false
    }).keys.head
  }

  def findString(value: String): Int = {
    info.filter(entry => entry._2 match {
      case string: StringInfo =>
        string.value.equals(value)
      case _ => false
    }).keys.head
  }

  def insertUTF8IfAbsent(value: String): Unit = {
    var index = -1
    info
      .filter(_._2.isInstanceOf[UTF8Info])
      .filter(_._2.asInstanceOf[UTF8Info].value.equals(value))
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = info.keys.max
      index = max + (if (info(max).wide) 2 else 1)
      insert(index, UTF8Info(value))
    }
  }

  def insertStringIfAbsent(value: String, constantPoolKind: ConstantPoolKind): Unit = {
    insertUTF8IfAbsent(value)
    var index = -1
    info
      .filter(_._2.isInstanceOf[StringInfo])
      .filter(_._2.kind == constantPoolKind)
      .filter(_._2.asInstanceOf[StringInfo].value.equals(value))
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = info.keys.max
      index = max + (if (info(max).wide) 2 else 1)
      insert(index, new StringInfo(value, constantPoolKind))
    }
  }

  def insertIfAbsent(constantPoolInfo: ConstantPoolInfo): Unit = {
    var index = -1
    info
      .filter(_._2.equals(constantPoolInfo))
      .keys
      .foreach(index = _)
    if (index == -1) {
      val max = info.keys.max
      index = max + (if (info(max).wide) 2 else 1)
      insert(index, constantPoolInfo)
    }
  }

  def findInfo(constantPoolInfo: ConstantPoolInfo): Int =
    info
      .filter(_._2.equals(constantPoolInfo))
      .keys
      .head

  def findString(value: String, constantPoolKind: ConstantPoolKind): Int =
   info
    .filter(_._2.isInstanceOf[StringInfo])
    .filter(_._2.kind == constantPoolKind)
    .filter(_._2.asInstanceOf[StringInfo].value.equals(value))
    .keys
    .head

  def readUTF8(index: Int): String = {
    info(index).asInstanceOf[UTF8Info].value
  }

  def readString(index: Int): String = {
    info(info(index).asInstanceOf[StringInfo].index).asInstanceOf[UTF8Info].value
  }

  def lastWide: Boolean =
    last match {
      case Some(value) =>
        last = None
        value.wide
      case None => false
    }

  override def toString = s"ConstantPool($info)"
}