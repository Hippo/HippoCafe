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

package rip.hippo.hippocafe.disassembler.instruction

import scala.language.implicitConversions

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
object BytecodeOpcode extends Enumeration {

  private implicit def toValue(opcode: Int): Value = Value(opcode)

  type BytecodeOpcode = Value

  val AALOAD: BytecodeOpcode = 0x32
  val AASTORE: BytecodeOpcode = 0x53

  val ACONST_NULL: BytecodeOpcode = 0x1

  val ALOAD: BytecodeOpcode = 0x19
  val ALOAD_0: BytecodeOpcode = 0x2a
  val ALOAD_1: BytecodeOpcode = 0x2b
  val ALOAD_2: BytecodeOpcode = 0x2c
  val ALOAD_3: BytecodeOpcode = 0x2d

  val ANEWARRAY: BytecodeOpcode = 0xbd
  val ARETURN: BytecodeOpcode = 0xb0
  val ARRAYLENGTH: BytecodeOpcode = 0xbe

  val ASTORE: BytecodeOpcode = 0x3a
  val ASTORE_0: BytecodeOpcode = 0x4b
  val ASTORE_1: BytecodeOpcode = 0x4c
  val ASTORE_2: BytecodeOpcode = 0x4d
  val ASTORE_3: BytecodeOpcode = 0x4e

  val ATHROW: BytecodeOpcode = 0xbf

  val BALOAD: BytecodeOpcode = 0x33
  val BASTORE: BytecodeOpcode = 0x54
  val BIPUSH: BytecodeOpcode = 0x10

  val CALOAD: BytecodeOpcode = 0x34
  val CASTORE: BytecodeOpcode = 0x55

  val CHECKCAST: BytecodeOpcode = 0xc0

  val D2F: BytecodeOpcode = 0x90
  val D2I: BytecodeOpcode = 0x8e
  val D2L: BytecodeOpcode = 0x8f
  val DADD: BytecodeOpcode = 0x63
  val DALOAD: BytecodeOpcode = 0x31
  val DASTORE: BytecodeOpcode = 0x52
  val DCMPG: BytecodeOpcode = 0x98
  val DCMPL: BytecodeOpcode = 0x97
  val DCONST_0: BytecodeOpcode = 0xe
  val DCONST_1: BytecodeOpcode = 0xf
  val DDIV: BytecodeOpcode = 0x6f
  val DLOAD: BytecodeOpcode = 0x18
  val DLOAD_0: BytecodeOpcode = 0x26
  val DLOAD_1: BytecodeOpcode = 0x27
  val DLOAD_2: BytecodeOpcode = 0x28
  val DLOAD_3: BytecodeOpcode = 0x29
  val DMUL: BytecodeOpcode = 0x6b
  val DNEG: BytecodeOpcode = 0x77
  val DREM: BytecodeOpcode = 0x73
  val DRETURN: BytecodeOpcode = 0xaf
  val DSTORE: BytecodeOpcode = 0x39
  val DSTORE_0: BytecodeOpcode = 0x47
  val DSTORE_1: BytecodeOpcode = 0x48
  val DSTORE_2: BytecodeOpcode = 0x49
  val DSTORE_3: BytecodeOpcode = 0x4a
  val DSUB: BytecodeOpcode = 0x67

  val DUP: BytecodeOpcode = 0x59
  val DUP_X1: BytecodeOpcode = 0x5a
  val DUP_X2: BytecodeOpcode = 0x5b
  val DUP2: BytecodeOpcode = 0x5c
  val DUP2_X1: BytecodeOpcode = 0x5d
  val DUP2_X2: BytecodeOpcode = 0x5e

  val F2D: BytecodeOpcode = 0x8d
  val F2I: BytecodeOpcode = 0x8b
  val F2L: BytecodeOpcode = 0x8c
  val FADD: BytecodeOpcode = 0x62
  val FALOAD: BytecodeOpcode = 0x30
  val FASTORE: BytecodeOpcode = 0x51
  val FCMPG: BytecodeOpcode = 0x96
  val FCMPL: BytecodeOpcode = 0x95
  val FCONST_0: BytecodeOpcode = 0xb
  val FCONST_1: BytecodeOpcode = 0xc
  val FCONST_2: BytecodeOpcode = 0xd
  val FDIV: BytecodeOpcode = 0x6e
  val FLOAD: BytecodeOpcode = 0x17
  val FLOAD_0: BytecodeOpcode = 0x22
  val FLOAD_1: BytecodeOpcode = 0x23
  val FLOAD_2: BytecodeOpcode = 0x24
  val FLOAD_3: BytecodeOpcode = 0x25
  val FMUL: BytecodeOpcode = 0x6a
  val FNEG: BytecodeOpcode = 0x76
  val FREM: BytecodeOpcode = 0x72
  val FRETURN: BytecodeOpcode = 0xae
  val FSTORE: BytecodeOpcode = 0x38
  val FSTORE_0: BytecodeOpcode = 0x43
  val FSTORE_1: BytecodeOpcode = 0x44
  val FSTORE_2: BytecodeOpcode = 0x45
  val FSTORE_3: BytecodeOpcode = 0x46
  val FSUB: BytecodeOpcode = 0x66

  val GETFIELD: BytecodeOpcode = 0xb4
  val GETSTATIC: BytecodeOpcode = 0xb2

  val GOTO: BytecodeOpcode = 0xa7
  val GOTO_W: BytecodeOpcode = 0xc8

  val I2B: BytecodeOpcode = 0x91
  val I2C: BytecodeOpcode = 0x92
  val I2D: BytecodeOpcode = 0x87
  val I2F: BytecodeOpcode = 0x86
  val I2L: BytecodeOpcode = 0x85
  val I2S: BytecodeOpcode = 0x93
  val IADD: BytecodeOpcode = 0x60
  val IALOAD: BytecodeOpcode = 0x2e
  val IAND: BytecodeOpcode = 0x7e
  val IASTORE: BytecodeOpcode = 0x4f
  val ICONST_M1: BytecodeOpcode = 0x2
  val ICONST_0: BytecodeOpcode = 0x3
  val ICONST_1: BytecodeOpcode = 0x4
  val ICONST_2: BytecodeOpcode = 0x5
  val ICONST_3: BytecodeOpcode = 0x6
  val ICONST_4: BytecodeOpcode = 0x7
  val ICONST_5: BytecodeOpcode = 0x8
  val IDIV: BytecodeOpcode = 0x6c

  val IF_ACMPEQ: BytecodeOpcode = 0xa5
  val IF_ACMPNE: BytecodeOpcode = 0xa6
  val IF_ICMPEQ: BytecodeOpcode = 0x9f
  val IF_ICMPNE: BytecodeOpcode = 0xa0
  val IF_ICMPLT: BytecodeOpcode = 0xa1
  val IF_ICMPGE: BytecodeOpcode = 0xa2
  val IF_ICMPGT: BytecodeOpcode = 0xa3
  val IF_ICMPLE: BytecodeOpcode = 0xa4
  val IFEQ: BytecodeOpcode = 0x99
  val IFNE: BytecodeOpcode = 0x9a
  val IFLT: BytecodeOpcode = 0x9b
  val IFGE: BytecodeOpcode = 0x9c
  val IFGT: BytecodeOpcode = 0x9d
  val IFLE: BytecodeOpcode = 0x9e
  val IFNONNULL: BytecodeOpcode = 0xc7
  val IFNULL: BytecodeOpcode = 0xc6

  val IINC: BytecodeOpcode = 0x84

  val ILOAD: BytecodeOpcode = 0x15
  val ILOAD_0: BytecodeOpcode = 0x1a
  val ILOAD_1: BytecodeOpcode = 0x1b
  val ILOAD_2: BytecodeOpcode = 0x1c
  val ILOAD_3: BytecodeOpcode = 0x1d

  val IMUL: BytecodeOpcode = 0x68
  val INEG: BytecodeOpcode = 0x74

  val INSTANCEOF: BytecodeOpcode = 0xc1
  val INVOKEDYNAMIC: BytecodeOpcode = 0xba
  val INVOKEINTERFACE: BytecodeOpcode = 0xb9
  val INVOKESPECIAL: BytecodeOpcode = 0xb7
  val INVOKESTATIC: BytecodeOpcode = 0xb8
  val INVOKEVIRTUAL: BytecodeOpcode = 0xb6

  val IOR: BytecodeOpcode = 0x80
  val IREM: BytecodeOpcode = 0x70

  val IRETURN: BytecodeOpcode = 0xac

  val ISHL: BytecodeOpcode = 0x78
  val ISHR: BytecodeOpcode = 0x7a

  val ISTORE: BytecodeOpcode = 0x36
  val ISTORE_0: BytecodeOpcode = 0x3b
  val ISTORE_1: BytecodeOpcode = 0x3c
  val ISTORE_2: BytecodeOpcode = 0x3d
  val ISTORE_3: BytecodeOpcode = 0x3e

  val ISUB: BytecodeOpcode = 0x64
  val IUSHR: BytecodeOpcode = 0x7c
  val IXOR: BytecodeOpcode = 0x82

  val JSR: BytecodeOpcode = 0xa8
  val JSR_W: BytecodeOpcode = 0xc9

  val L2D: BytecodeOpcode = 0x8a
  val L2F: BytecodeOpcode = 0x89
  val L2I: BytecodeOpcode = 0x88
  val LADD: BytecodeOpcode = 0x61
  val LALOAD: BytecodeOpcode = 0x2f
  val LAND: BytecodeOpcode = 0x7f
  val LASTORE: BytecodeOpcode = 0x50
  val LCMP: BytecodeOpcode = 0x94
  val LCONST_0: BytecodeOpcode = 0x9
  val LCONST_1: BytecodeOpcode = 0xa

  val LDC: BytecodeOpcode = 0x12
  val LDC_W: BytecodeOpcode = 0x13
  val LDC2_W: BytecodeOpcode = 0x14

  val LDIV: BytecodeOpcode = 0x6d

  val LLOAD: BytecodeOpcode = 0x16
  val LLOAD_0: BytecodeOpcode = 0x1e
  val LLOAD_1: BytecodeOpcode = 0x1f
  val LLOAD_2: BytecodeOpcode = 0x20
  val LLOAD_3: BytecodeOpcode = 0x21

  val LMUL: BytecodeOpcode = 0x69
  val LNEG: BytecodeOpcode = 0x75

  val LOOKUPSWITCH: BytecodeOpcode = 0xab // "The CIA wants you to think it's the same as if-else, it's not" - Terry Davis

  val LOR: BytecodeOpcode = 0x81
  val LREM: BytecodeOpcode = 0x71

  val LRETURN: BytecodeOpcode = 0xad

  val LSHL: BytecodeOpcode = 0x79
  val LSHR: BytecodeOpcode = 0x7b

  val LSTORE: BytecodeOpcode = 0x37
  val LSTORE_0: BytecodeOpcode = 0x3f
  val LSTORE_1: BytecodeOpcode = 0x40
  val LSTORE_2: BytecodeOpcode = 0x41
  val LSTORE_3: BytecodeOpcode = 0x42

  val LSUB: BytecodeOpcode = 0x65
  val LUSHR: BytecodeOpcode = 0x7d
  val LXOR: BytecodeOpcode = 0x83

  val MONITORENTER: BytecodeOpcode = 0xc2
  val MONITOREXIT: BytecodeOpcode = 0xc3

  val MULTIANEWARRAY: BytecodeOpcode = 0xc5
  val NEW: BytecodeOpcode = 0xbb
  val NEWARRAY: BytecodeOpcode = 0xbc

  val NOP: BytecodeOpcode = 0x0

  val POP: BytecodeOpcode = 0x57
  val POP2: BytecodeOpcode = 0x58

  val PUTFIELD: BytecodeOpcode = 0xb5
  val PUTSTATIC: BytecodeOpcode = 0xb3

  val RET: BytecodeOpcode = 0xa9

  val RETURN: BytecodeOpcode = 0xb1

  val SALOAD: BytecodeOpcode = 0x35
  val SASTORE: BytecodeOpcode = 0x56
  val SIPUSH: BytecodeOpcode = 0x11

  val SWAP: BytecodeOpcode = 0x5f

  val TABLESWITCH: BytecodeOpcode = 0xaa

  val WIDE: BytecodeOpcode = 0xc4

  def fromOpcode(opcode: Int): Option[BytecodeOpcode] =
    BytecodeOpcode.values.find(_.id == (opcode & 0xFF))


}
