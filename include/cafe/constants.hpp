#pragma once

#include <cstdint>
#include <string>

namespace cafe {
namespace access_flag {
constexpr uint16_t acc_public = 0x0001;
constexpr uint16_t acc_private = 0x0002;
constexpr uint16_t acc_protected = 0x0004;
constexpr uint16_t acc_static = 0x0008;
constexpr uint16_t acc_final = 0x0010;
constexpr uint16_t acc_super = 0x0020;
constexpr uint16_t acc_synchronized = 0x0020;
constexpr uint16_t acc_open = 0x0020;
constexpr uint16_t acc_transitive = 0x0020;
constexpr uint16_t acc_volatile = 0x0040;
constexpr uint16_t acc_bridge = 0x0040;
constexpr uint16_t acc_static_phase = 0x0040;
constexpr uint16_t acc_varargs = 0x0080;
constexpr uint16_t acc_transient = 0x0080;
constexpr uint16_t acc_native = 0x0100;
constexpr uint16_t acc_interface = 0x0200;
constexpr uint16_t acc_abstract = 0x0400;
constexpr uint16_t acc_strict = 0x0800;
constexpr uint16_t acc_synthetic = 0x1000;
constexpr uint16_t acc_annotation = 0x2000;
constexpr uint16_t acc_enum = 0x4000;
constexpr uint16_t acc_mandated = 0x8000;
constexpr uint16_t acc_module = 0x8000;
} // namespace access_flag

namespace class_version {
constexpr uint32_t v1_1 = 45 << 16 | 3;
constexpr uint32_t v1_2 = 46 << 16;
constexpr uint32_t v1_3 = 47 << 16;
constexpr uint32_t v1_4 = 48 << 16;
constexpr uint32_t v5 = 49 << 16;
constexpr uint32_t v6 = 50 << 16;
constexpr uint32_t v7 = 51 << 16;
constexpr uint32_t v8 = 52 << 16;
constexpr uint32_t v9 = 53 << 16;
constexpr uint32_t v10 = 54 << 16;
constexpr uint32_t v11 = 55 << 16;
constexpr uint32_t v12 = 56 << 16;
constexpr uint32_t v13 = 57 << 16;
constexpr uint32_t v14 = 58 << 16;
constexpr uint32_t v15 = 59 << 16;
constexpr uint32_t v16 = 60 << 16;
constexpr uint32_t v17 = 61 << 16;
constexpr uint32_t v18 = 62 << 16;
constexpr uint32_t v19 = 63 << 16;
constexpr uint32_t v20 = 64 << 16;
constexpr uint32_t v21 = 65 << 16;
constexpr uint32_t v22 = 66 << 16;
constexpr uint32_t v23 = 67 << 16;
} // namespace class_version

namespace typepath {
constexpr uint8_t array = 0;
constexpr uint8_t nested = 1;
constexpr uint8_t wildcard = 2;
constexpr uint8_t type_argument = 3;
} // namespace typepath

namespace reference_kind {
constexpr uint8_t get_field = 1;
constexpr uint8_t get_static = 2;
constexpr uint8_t put_field = 3;
constexpr uint8_t put_static = 4;
constexpr uint8_t invoke_virtual = 5;
constexpr uint8_t invoke_static = 6;
constexpr uint8_t invoke_special = 7;
constexpr uint8_t new_invoke_special = 8;
constexpr uint8_t invoke_interface = 9;
} // namespace reference_kind

namespace op {
constexpr uint8_t aaload = 0x32;
constexpr uint8_t aastore = 0x53;
constexpr uint8_t aconst_null = 0x01;
constexpr uint8_t aload = 0x19;
constexpr uint8_t aload_0 = 0x2a;
constexpr uint8_t aload_1 = 0x2b;
constexpr uint8_t aload_2 = 0x2c;
constexpr uint8_t aload_3 = 0x2d;
constexpr uint8_t anewarray = 0xbd;
constexpr uint8_t areturn = 0xb0;
constexpr uint8_t arraylength = 0xbe;
constexpr uint8_t astore = 0x3a;
constexpr uint8_t astore_0 = 0x4b;
constexpr uint8_t astore_1 = 0x4c;
constexpr uint8_t astore_2 = 0x4d;
constexpr uint8_t astore_3 = 0x4e;
constexpr uint8_t athrow = 0xbf;
constexpr uint8_t baload = 0x33;
constexpr uint8_t bastore = 0x54;
constexpr uint8_t bipush = 0x10;
constexpr uint8_t caload = 0x34;
constexpr uint8_t castore = 0x55;
constexpr uint8_t checkcast = 0xc0;
constexpr uint8_t d2f = 0x90;
constexpr uint8_t d2i = 0x8e;
constexpr uint8_t d2l = 0x8f;
constexpr uint8_t dadd = 0x63;
constexpr uint8_t daload = 0x31;
constexpr uint8_t dastore = 0x52;
constexpr uint8_t dcmpg = 0x98;
constexpr uint8_t dcmpl = 0x97;
constexpr uint8_t dconst_0 = 0x0e;
constexpr uint8_t dconst_1 = 0x0f;
constexpr uint8_t ddiv = 0x6f;
constexpr uint8_t dload = 0x18;
constexpr uint8_t dload_0 = 0x26;
constexpr uint8_t dload_1 = 0x27;
constexpr uint8_t dload_2 = 0x28;
constexpr uint8_t dload_3 = 0x29;
constexpr uint8_t dmul = 0x6b;
constexpr uint8_t dneg = 0x77;
constexpr uint8_t drem = 0x73;
constexpr uint8_t dreturn = 0xaf;
constexpr uint8_t dstore = 0x39;
constexpr uint8_t dstore_0 = 0x47;
constexpr uint8_t dstore_1 = 0x48;
constexpr uint8_t dstore_2 = 0x49;
constexpr uint8_t dstore_3 = 0x4a;
constexpr uint8_t dsub = 0x67;
constexpr uint8_t dup = 0x59;
constexpr uint8_t dup_x1 = 0x5a;
constexpr uint8_t dup_x2 = 0x5b;
constexpr uint8_t dup2 = 0x5c;
constexpr uint8_t dup2_x1 = 0x5d;
constexpr uint8_t dup2_x2 = 0x5e;
constexpr uint8_t f2d = 0x8d;
constexpr uint8_t f2i = 0x8b;
constexpr uint8_t f2l = 0x8c;
constexpr uint8_t fadd = 0x62;
constexpr uint8_t faload = 0x30;
constexpr uint8_t fastore = 0x51;
constexpr uint8_t fcmpg = 0x96;
constexpr uint8_t fcmpl = 0x95;
constexpr uint8_t fconst_0 = 0x0b;
constexpr uint8_t fconst_1 = 0x0c;
constexpr uint8_t fconst_2 = 0x0d;
constexpr uint8_t fdiv = 0x6e;
constexpr uint8_t fload = 0x17;
constexpr uint8_t fload_0 = 0x22;
constexpr uint8_t fload_1 = 0x23;
constexpr uint8_t fload_2 = 0x24;
constexpr uint8_t fload_3 = 0x25;
constexpr uint8_t fmul = 0x6a;
constexpr uint8_t fneg = 0x76;
constexpr uint8_t frem = 0x72;
constexpr uint8_t freturn = 0xae;
constexpr uint8_t fstore = 0x38;
constexpr uint8_t fstore_0 = 0x43;
constexpr uint8_t fstore_1 = 0x44;
constexpr uint8_t fstore_2 = 0x45;
constexpr uint8_t fstore_3 = 0x46;
constexpr uint8_t fsub = 0x66;
constexpr uint8_t getfield = 0xb4;
constexpr uint8_t getstatic = 0xb2;
constexpr uint8_t goto_ = 0xa7;
constexpr uint8_t goto_w = 0xc8;
constexpr uint8_t i2b = 0x91;
constexpr uint8_t i2c = 0x92;
constexpr uint8_t i2d = 0x87;
constexpr uint8_t i2f = 0x86;
constexpr uint8_t i2l = 0x85;
constexpr uint8_t i2s = 0x93;
constexpr uint8_t iadd = 0x60;
constexpr uint8_t iaload = 0x2e;
constexpr uint8_t iand = 0x7e;
constexpr uint8_t iastore = 0x4f;
constexpr uint8_t iconst_m1 = 0x02;
constexpr uint8_t iconst_0 = 0x03;
constexpr uint8_t iconst_1 = 0x04;
constexpr uint8_t iconst_2 = 0x05;
constexpr uint8_t iconst_3 = 0x06;
constexpr uint8_t iconst_4 = 0x07;
constexpr uint8_t iconst_5 = 0x08;
constexpr uint8_t idiv = 0x6c;
constexpr uint8_t if_acmpeq = 0xa5;
constexpr uint8_t if_acmpne = 0xa6;
constexpr uint8_t if_icmpeq = 0x9f;
constexpr uint8_t if_icmpne = 0xa0;
constexpr uint8_t if_icmplt = 0xa1;
constexpr uint8_t if_icmpge = 0xa2;
constexpr uint8_t if_icmpgt = 0xa3;
constexpr uint8_t if_icmple = 0xa4;
constexpr uint8_t ifeq = 0x99;
constexpr uint8_t ifne = 0x9a;
constexpr uint8_t iflt = 0x9b;
constexpr uint8_t ifge = 0x9c;
constexpr uint8_t ifgt = 0x9d;
constexpr uint8_t ifle = 0x9e;
constexpr uint8_t ifnonnull = 0xc7;
constexpr uint8_t ifnull = 0xc6;
constexpr uint8_t iinc = 0x84;
constexpr uint8_t iload = 0x15;
constexpr uint8_t iload_0 = 0x1a;
constexpr uint8_t iload_1 = 0x1b;
constexpr uint8_t iload_2 = 0x1c;
constexpr uint8_t iload_3 = 0x1d;
constexpr uint8_t imul = 0x68;
constexpr uint8_t ineg = 0x74;
constexpr uint8_t instanceof = 0xc1;
constexpr uint8_t invokedynamic = 0xba;
constexpr uint8_t invokeinterface = 0xb9;
constexpr uint8_t invokespecial = 0xb7;
constexpr uint8_t invokestatic = 0xb8;
constexpr uint8_t invokevirtual = 0xb6;
constexpr uint8_t ior = 0x80;
constexpr uint8_t irem = 0x70;
constexpr uint8_t ireturn = 0xac;
constexpr uint8_t ishl = 0x78;
constexpr uint8_t ishr = 0x7a;
constexpr uint8_t istore = 0x36;
constexpr uint8_t istore_0 = 0x3b;
constexpr uint8_t istore_1 = 0x3c;
constexpr uint8_t istore_2 = 0x3d;
constexpr uint8_t istore_3 = 0x3e;
constexpr uint8_t isub = 0x64;
constexpr uint8_t iushr = 0x7c;
constexpr uint8_t ixor = 0x82;
constexpr uint8_t jsr = 0xa8;
constexpr uint8_t jsr_w = 0xc9;
constexpr uint8_t l2d = 0x8a;
constexpr uint8_t l2f = 0x89;
constexpr uint8_t l2i = 0x88;
constexpr uint8_t ladd = 0x61;
constexpr uint8_t laload = 0x2f;
constexpr uint8_t land = 0x7f;
constexpr uint8_t lastore = 0x50;
constexpr uint8_t lcmp = 0x94;
constexpr uint8_t lconst_0 = 0x09;
constexpr uint8_t lconst_1 = 0x0a;
constexpr uint8_t ldc = 0x12;
constexpr uint8_t ldc_w = 0x13;
constexpr uint8_t ldc2_w = 0x14;
constexpr uint8_t ldiv = 0x6d;
constexpr uint8_t lload = 0x16;
constexpr uint8_t lload_0 = 0x1e;
constexpr uint8_t lload_1 = 0x1f;
constexpr uint8_t lload_2 = 0x20;
constexpr uint8_t lload_3 = 0x21;
constexpr uint8_t lmul = 0x69;
constexpr uint8_t lneg = 0x75;
constexpr uint8_t lookupswitch = 0xab;
constexpr uint8_t lor = 0x81;
constexpr uint8_t lrem = 0x71;
constexpr uint8_t lreturn = 0xad;
constexpr uint8_t lshl = 0x79;
constexpr uint8_t lshr = 0x7b;
constexpr uint8_t lstore = 0x37;
constexpr uint8_t lstore_0 = 0x3f;
constexpr uint8_t lstore_1 = 0x40;
constexpr uint8_t lstore_2 = 0x41;
constexpr uint8_t lstore_3 = 0x42;
constexpr uint8_t lsub = 0x65;
constexpr uint8_t lushr = 0x7d;
constexpr uint8_t lxor = 0x83;
constexpr uint8_t monitorenter = 0xc2;
constexpr uint8_t monitorexit = 0xc3;
constexpr uint8_t multianewarray = 0xc5;
constexpr uint8_t new_ = 0xbb;
constexpr uint8_t newarray = 0xbc;
constexpr uint8_t nop = 0x00;
constexpr uint8_t pop = 0x57;
constexpr uint8_t pop2 = 0x58;
constexpr uint8_t putfield = 0xb5;
constexpr uint8_t putstatic = 0xb3;
constexpr uint8_t ret = 0xa9;
constexpr uint8_t return_ = 0xb1;
constexpr uint8_t saload = 0x35;
constexpr uint8_t sastore = 0x56;
constexpr uint8_t sipush = 0x11;
constexpr uint8_t swap = 0x5f;
constexpr uint8_t tableswitch = 0xaa;
constexpr uint8_t wide = 0xc4;
} // namespace op

std::string opcode_name(int16_t opcode);

namespace array {
constexpr uint8_t t_boolean = 4;
constexpr uint8_t t_char = 5;
constexpr uint8_t t_float = 6;
constexpr uint8_t t_double = 7;
constexpr uint8_t t_byte = 8;
constexpr uint8_t t_short = 9;
constexpr uint8_t t_int = 10;
constexpr uint8_t t_long = 11;
} // namespace array

} // namespace cafe
