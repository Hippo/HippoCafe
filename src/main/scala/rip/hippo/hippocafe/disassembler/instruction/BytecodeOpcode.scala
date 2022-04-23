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

/**
 * @author Hippo
 * @version 1.0.0, 8/4/20
 * @since 1.0.0
 */
enum BytecodeOpcode(val opcode: Int) {

  case NOP extends BytecodeOpcode(0x0)
  case ACONST_NULL extends BytecodeOpcode(0x1)
  case ICONST_M1 extends BytecodeOpcode(0x2)
  case ICONST_0 extends BytecodeOpcode(0x3)
  case ICONST_1 extends BytecodeOpcode(0x4)
  case ICONST_2 extends BytecodeOpcode(0x5)
  case ICONST_3 extends BytecodeOpcode(0x6)
  case ICONST_4 extends BytecodeOpcode(0x7)
  case ICONST_5 extends BytecodeOpcode(0x8)
  case LCONST_0 extends BytecodeOpcode(0x9)
  case LCONST_1 extends BytecodeOpcode(0xA)
  case FCONST_0 extends BytecodeOpcode(0xB)
  case FCONST_1 extends BytecodeOpcode(0xC)
  case FCONST_2 extends BytecodeOpcode(0xD)
  case DCONST_0 extends BytecodeOpcode(0xE)
  case DCONST_1 extends BytecodeOpcode(0xF)
  case BIPUSH extends BytecodeOpcode(0x10)
  case SIPUSH extends BytecodeOpcode(0x11)
  case LDC extends BytecodeOpcode(0x12)
  case LDC_W extends BytecodeOpcode(0x13)
  case LDC2_W extends BytecodeOpcode(0x14)
  case ILOAD extends BytecodeOpcode(0x15)
  case LLOAD extends BytecodeOpcode(0x16)
  case FLOAD extends BytecodeOpcode(0x17)
  case DLOAD extends BytecodeOpcode(0x18)
  case ALOAD extends BytecodeOpcode(0x19)
  case ILOAD_0 extends BytecodeOpcode(0x1A)
  case ILOAD_1 extends BytecodeOpcode(0x1B)
  case ILOAD_2 extends BytecodeOpcode(0x1C)
  case ILOAD_3 extends BytecodeOpcode(0x1D)
  case LLOAD_0 extends BytecodeOpcode(0x1E)
  case LLOAD_1 extends BytecodeOpcode(0x1F)
  case LLOAD_2 extends BytecodeOpcode(0x20)
  case LLOAD_3 extends BytecodeOpcode(0x21)
  case FLOAD_0 extends BytecodeOpcode(0x22)
  case FLOAD_1 extends BytecodeOpcode(0x23)
  case FLOAD_2 extends BytecodeOpcode(0x24)
  case FLOAD_3 extends BytecodeOpcode(0x25)
  case DLOAD_0 extends BytecodeOpcode(0x26)
  case DLOAD_1 extends BytecodeOpcode(0x27)
  case DLOAD_2 extends BytecodeOpcode(0x28)
  case DLOAD_3 extends BytecodeOpcode(0x29)
  case ALOAD_0 extends BytecodeOpcode(0x2A)
  case ALOAD_1 extends BytecodeOpcode(0x2B)
  case ALOAD_2 extends BytecodeOpcode(0x2C)
  case ALOAD_3 extends BytecodeOpcode(0x2D)
  case IALOAD extends BytecodeOpcode(0x2E)
  case LALOAD extends BytecodeOpcode(0x2F)
  case FALOAD extends BytecodeOpcode(0x30)
  case DALOAD extends BytecodeOpcode(0x31)
  case AALOAD extends BytecodeOpcode(0x32)
  case BALOAD extends BytecodeOpcode(0x33)
  case CALOAD extends BytecodeOpcode(0x34)
  case SALOAD extends BytecodeOpcode(0x35)
  case ISTORE extends BytecodeOpcode(0x36)
  case LSTORE extends BytecodeOpcode(0x37)
  case FSTORE extends BytecodeOpcode(0x38)
  case DSTORE extends BytecodeOpcode(0x39)
  case ASTORE extends BytecodeOpcode(0x3A)
  case ISTORE_0 extends BytecodeOpcode(0x3B)
  case ISTORE_1 extends BytecodeOpcode(0x3C)
  case ISTORE_2 extends BytecodeOpcode(0x3D)
  case ISTORE_3 extends BytecodeOpcode(0x3E)
  case LSTORE_0 extends BytecodeOpcode(0x3F)
  case LSTORE_1 extends BytecodeOpcode(0x40)
  case LSTORE_2 extends BytecodeOpcode(0x41)
  case LSTORE_3 extends BytecodeOpcode(0x42)
  case FSTORE_0 extends BytecodeOpcode(0x43)
  case FSTORE_1 extends BytecodeOpcode(0x44)
  case FSTORE_2 extends BytecodeOpcode(0x45)
  case FSTORE_3 extends BytecodeOpcode(0x46)
  case DSTORE_0 extends BytecodeOpcode(0x47)
  case DSTORE_1 extends BytecodeOpcode(0x48)
  case DSTORE_2 extends BytecodeOpcode(0x49)
  case DSTORE_3 extends BytecodeOpcode(0x4A)
  case ASTORE_0 extends BytecodeOpcode(0x4B)
  case ASTORE_1 extends BytecodeOpcode(0x4C)
  case ASTORE_2 extends BytecodeOpcode(0x4D)
  case ASTORE_3 extends BytecodeOpcode(0x4E)
  case IASTORE extends BytecodeOpcode(0x4F)
  case LASTORE extends BytecodeOpcode(0x50)
  case FASTORE extends BytecodeOpcode(0x51)
  case DASTORE extends BytecodeOpcode(0x52)
  case AASTORE extends BytecodeOpcode(0x53)
  case BASTORE extends BytecodeOpcode(0x54)
  case CASTORE extends BytecodeOpcode(0x55)
  case SASTORE extends BytecodeOpcode(0x56)
  case POP extends BytecodeOpcode(0x57)
  case POP2 extends BytecodeOpcode(0x58)
  case DUP extends BytecodeOpcode(0x59)
  case DUP_X1 extends BytecodeOpcode(0x5A)
  case DUP_X2 extends BytecodeOpcode(0x5B)
  case DUP2 extends BytecodeOpcode(0x5C)
  case DUP2_X1 extends BytecodeOpcode(0x5D)
  case DUP2_X2 extends BytecodeOpcode(0x5E)
  case SWAP extends BytecodeOpcode(0x5F)
  case IADD extends BytecodeOpcode(0x60)
  case LADD extends BytecodeOpcode(0x61)
  case FADD extends BytecodeOpcode(0x62)
  case DADD extends BytecodeOpcode(0x63)
  case ISUB extends BytecodeOpcode(0x64)
  case LSUB extends BytecodeOpcode(0x65)
  case FSUB extends BytecodeOpcode(0x66)
  case DSUB extends BytecodeOpcode(0x67)
  case IMUL extends BytecodeOpcode(0x68)
  case LMUL extends BytecodeOpcode(0x69)
  case FMUL extends BytecodeOpcode(0x6A)
  case DMUL extends BytecodeOpcode(0x6B)
  case IDIV extends BytecodeOpcode(0x6C)
  case LDIV extends BytecodeOpcode(0x6D)
  case FDIV extends BytecodeOpcode(0x6E)
  case DDIV extends BytecodeOpcode(0x6F)
  case IREM extends BytecodeOpcode(0x70)
  case LREM extends BytecodeOpcode(0x71)
  case FREM extends BytecodeOpcode(0x72)
  case DREM extends BytecodeOpcode(0x73)
  case INEG extends BytecodeOpcode(0x74)
  case LNEG extends BytecodeOpcode(0x75)
  case FNEG extends BytecodeOpcode(0x76)
  case DNEG extends BytecodeOpcode(0x77)
  case ISHL extends BytecodeOpcode(0x78)
  case LSHL extends BytecodeOpcode(0x79)
  case ISHR extends BytecodeOpcode(0x7A)
  case LSHR extends BytecodeOpcode(0x7B)
  case IUSHR extends BytecodeOpcode(0x7C)
  case LUSHR extends BytecodeOpcode(0x7D)
  case IAND extends BytecodeOpcode(0x7E)
  case LAND extends BytecodeOpcode(0x7F)
  case IOR extends BytecodeOpcode(0x80)
  case LOR extends BytecodeOpcode(0x81)
  case IXOR extends BytecodeOpcode(0x82)
  case LXOR extends BytecodeOpcode(0x83)
  case IINC extends BytecodeOpcode(0x84)
  case I2L extends BytecodeOpcode(0x85)
  case I2F extends BytecodeOpcode(0x86)
  case I2D extends BytecodeOpcode(0x87)
  case L2I extends BytecodeOpcode(0x88)
  case L2F extends BytecodeOpcode(0x89)
  case L2D extends BytecodeOpcode(0x8A)
  case F2I extends BytecodeOpcode(0x8B)
  case F2L extends BytecodeOpcode(0x8C)
  case F2D extends BytecodeOpcode(0x8D)
  case D2I extends BytecodeOpcode(0x8E)
  case D2L extends BytecodeOpcode(0x8F)
  case D2F extends BytecodeOpcode(0x90)
  case I2B extends BytecodeOpcode(0x91)
  case I2C extends BytecodeOpcode(0x92)
  case I2S extends BytecodeOpcode(0x93)
  case LCMP extends BytecodeOpcode(0x94)
  case FCMPL extends BytecodeOpcode(0x95)
  case FCMPG extends BytecodeOpcode(0x96)
  case DCMPL extends BytecodeOpcode(0x97)
  case DCMPG extends BytecodeOpcode(0x98)
  case IFEQ extends BytecodeOpcode(0x99)
  case IFNE extends BytecodeOpcode(0x9A)
  case IFLT extends BytecodeOpcode(0x9B)
  case IFGE extends BytecodeOpcode(0x9C)
  case IFGT extends BytecodeOpcode(0x9D)
  case IFLE extends BytecodeOpcode(0x9E)
  case IF_ICMPEQ extends BytecodeOpcode(0x9F)
  case IF_ICMPNE extends BytecodeOpcode(0xA0)
  case IF_ICMPLT extends BytecodeOpcode(0xA1)
  case IF_ICMPGE extends BytecodeOpcode(0xA2)
  case IF_ICMPGT extends BytecodeOpcode(0xA3)
  case IF_ICMPLE extends BytecodeOpcode(0xA4)
  case IF_ACMPEQ extends BytecodeOpcode(0xA5)
  case IF_ACMPNE extends BytecodeOpcode(0xA6)
  case GOTO extends BytecodeOpcode(0xA7)
  case JSR extends BytecodeOpcode(0xA8)
  case RET extends BytecodeOpcode(0xA9)
  case TABLESWITCH extends BytecodeOpcode(0xAA)
  case LOOKUPSWITCH extends BytecodeOpcode(0xAB)
  case IRETURN extends BytecodeOpcode(0xAC)
  case LRETURN extends BytecodeOpcode(0xAD)
  case FRETURN extends BytecodeOpcode(0xAE)
  case DRETURN extends BytecodeOpcode(0xAF)
  case ARETURN extends BytecodeOpcode(0xB0)
  case RETURN extends BytecodeOpcode(0xB1)
  case GETSTATIC extends BytecodeOpcode(0xB2)
  case PUTSTATIC extends BytecodeOpcode(0xB3)
  case GETFIELD extends BytecodeOpcode(0xB4)
  case PUTFIELD extends BytecodeOpcode(0xB5)
  case INVOKEVIRTUAL extends BytecodeOpcode(0xB6)
  case INVOKESPECIAL extends BytecodeOpcode(0xB7)
  case INVOKESTATIC extends BytecodeOpcode(0xB8)
  case INVOKEINTERFACE extends BytecodeOpcode(0xB9)
  case INVOKEDYNAMIC extends BytecodeOpcode(0xBA)
  case NEW extends BytecodeOpcode(0xBB)
  case NEWARRAY extends BytecodeOpcode(0xBC)
  case ANEWARRAY extends BytecodeOpcode(0xBD)
  case ARRAYLENGTH extends BytecodeOpcode(0xBE)
  case ATHROW extends BytecodeOpcode(0xBF)
  case CHECKCAST extends BytecodeOpcode(0xC0)
  case INSTANCEOF extends BytecodeOpcode(0xC1)
  case MONITORENTER extends BytecodeOpcode(0xC2)
  case MONITOREXIT extends BytecodeOpcode(0xC3)
  case WIDE extends BytecodeOpcode(0xC4)
  case MULTIANEWARRAY extends BytecodeOpcode(0xC5)
  case IFNULL extends BytecodeOpcode(0xC6)
  case IFNONNULL extends BytecodeOpcode(0xC7)
  case GOTO_W extends BytecodeOpcode(0xC8)
  case JSR_W extends BytecodeOpcode(0xC9)

  // jvm internal opcodes
  case FAST_AGETFIELD extends BytecodeOpcode(0xCB)
  case FAST_BGETFIELD extends BytecodeOpcode(0xCC)
  case FAST_CGETFIELD extends BytecodeOpcode(0xCD)
  case FAST_DGETFIELD extends BytecodeOpcode(0xCE)
  case FAST_FGETFIELD extends BytecodeOpcode(0xCF)
  case FAST_IGETFIELD extends BytecodeOpcode(0xD0)
  case FAST_LGETFIELD extends BytecodeOpcode(0xD1)
  case FAST_SGETFIELD extends BytecodeOpcode(0xD2)
  case FAST_APUTFIELD extends BytecodeOpcode(0xD3)
  case FAST_BPUTFIELD extends BytecodeOpcode(0xD4)
  case FAST_ZPUTFIELD extends BytecodeOpcode(0xD5)
  case FAST_CPUTFIELD extends BytecodeOpcode(0xD6)
  case FAST_DPUTFIELD extends BytecodeOpcode(0xD7)
  case FAST_FPUTFIELD extends BytecodeOpcode(0xD8)
  case FAST_IPUTFIELD extends BytecodeOpcode(0xD9)
  case FAST_LPUTFIELD extends BytecodeOpcode(0xDA)
  case FAST_SPUTFIELD extends BytecodeOpcode(0xDB)
  case FAST_ALOAD_0 extends BytecodeOpcode(0xDC)
  case FAST_IACCESS_0 extends BytecodeOpcode(0xDD)
  case FAST_AACCESS_0 extends BytecodeOpcode(0xDE)
  case FAST_FACCESS_0 extends BytecodeOpcode(0xDF)
  case FAST_ILOAD extends BytecodeOpcode(0xE0)
  case FAST_ILOAD2 extends BytecodeOpcode(0xE1)
  case FAST_ICALOAD extends BytecodeOpcode(0xE2)
  case FAST_INVOKEVFINAL extends BytecodeOpcode(0xE3)
  case FAST_LINEARSWITCH extends BytecodeOpcode(0xE4)
  case FAST_BINARYSWITCH extends BytecodeOpcode(0xE5)
  case FAST_ALDC extends BytecodeOpcode(0xE6)
  case FAST_ALDC_W extends BytecodeOpcode(0xE7)
  case RETURN_REGISTER_FINALIZER extends BytecodeOpcode(0xE8)
  case INVOKEHANDLE extends BytecodeOpcode(0xE9)
  case NOFAST_GETFIELD extends BytecodeOpcode(0xEA)
  case NOFAST_PUTFIELD extends BytecodeOpcode(0xEB)
  case NOFAST_ALOAD_0 extends BytecodeOpcode(0xEC)
  case NOFAST_ILOAD extends BytecodeOpcode(0xED)
  case SHOULD_NOT_REACH_HERE extends BytecodeOpcode(0xEE)
}

object BytecodeOpcode {
  def fromOpcode(opcode: Int): Option[BytecodeOpcode] =
    BytecodeOpcode.values.find(_.opcode == (opcode & 0xFF))
}
