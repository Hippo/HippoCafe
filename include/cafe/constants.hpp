#pragma once

#include <cstdint>
#include <string>

namespace cafe {
namespace access_flag {
inline constexpr uint16_t acc_public = 0x0001;
inline constexpr uint16_t acc_private = 0x0002;
inline constexpr uint16_t acc_protected = 0x0004;
inline constexpr uint16_t acc_static = 0x0008;
inline constexpr uint16_t acc_final = 0x0010;
inline constexpr uint16_t acc_super = 0x0020;
inline constexpr uint16_t acc_synchronized = 0x0020;
inline constexpr uint16_t acc_open = 0x0020;
inline constexpr uint16_t acc_transitive = 0x0020;
inline constexpr uint16_t acc_volatile = 0x0040;
inline constexpr uint16_t acc_bridge = 0x0040;
inline constexpr uint16_t acc_static_phase = 0x0040;
inline constexpr uint16_t acc_varargs = 0x0080;
inline constexpr uint16_t acc_transient = 0x0080;
inline constexpr uint16_t acc_native = 0x0100;
inline constexpr uint16_t acc_interface = 0x0200;
inline constexpr uint16_t acc_abstract = 0x0400;
inline constexpr uint16_t acc_strict = 0x0800;
inline constexpr uint16_t acc_synthetic = 0x1000;
inline constexpr uint16_t acc_annotation = 0x2000;
inline constexpr uint16_t acc_enum = 0x4000;
inline constexpr uint16_t acc_mandated = 0x8000;
inline constexpr uint16_t acc_module = 0x8000;
} // namespace access_flag

namespace class_version {
inline constexpr uint32_t v1_1 = 45 << 16 | 3;
inline constexpr uint32_t v1_2 = 46 << 16;
inline constexpr uint32_t v1_3 = 47 << 16;
inline constexpr uint32_t v1_4 = 48 << 16;
inline constexpr uint32_t v5 = 49 << 16;
inline constexpr uint32_t v6 = 50 << 16;
inline constexpr uint32_t v7 = 51 << 16;
inline constexpr uint32_t v8 = 52 << 16;
inline constexpr uint32_t v9 = 53 << 16;
inline constexpr uint32_t v10 = 54 << 16;
inline constexpr uint32_t v11 = 55 << 16;
inline constexpr uint32_t v12 = 56 << 16;
inline constexpr uint32_t v13 = 57 << 16;
inline constexpr uint32_t v14 = 58 << 16;
inline constexpr uint32_t v15 = 59 << 16;
inline constexpr uint32_t v16 = 60 << 16;
inline constexpr uint32_t v17 = 61 << 16;
inline constexpr uint32_t v18 = 62 << 16;
inline constexpr uint32_t v19 = 63 << 16;
inline constexpr uint32_t v20 = 64 << 16;
inline constexpr uint32_t v21 = 65 << 16;
inline constexpr uint32_t v22 = 66 << 16;
inline constexpr uint32_t v23 = 67 << 16;
} // namespace class_version

namespace typepath {
inline constexpr uint8_t array = 0;
inline constexpr uint8_t nested = 1;
inline constexpr uint8_t wildcard = 2;
inline constexpr uint8_t type_argument = 3;
} // namespace typepath
std::string typepath_name(uint8_t path);

namespace reference_kind {
inline constexpr uint8_t get_field = 1;
inline constexpr uint8_t get_static = 2;
inline constexpr uint8_t put_field = 3;
inline constexpr uint8_t put_static = 4;
inline constexpr uint8_t invoke_virtual = 5;
inline constexpr uint8_t invoke_static = 6;
inline constexpr uint8_t invoke_special = 7;
inline constexpr uint8_t new_invoke_special = 8;
inline constexpr uint8_t invoke_interface = 9;
} // namespace reference_kind
std::string reference_kind_name(uint8_t kind);

namespace op {
inline constexpr uint8_t aaload = 0x32;
inline constexpr uint8_t aastore = 0x53;
inline constexpr uint8_t aconst_null = 0x01;
inline constexpr uint8_t aload = 0x19;
inline constexpr uint8_t aload_0 = 0x2a;
inline constexpr uint8_t aload_1 = 0x2b;
inline constexpr uint8_t aload_2 = 0x2c;
inline constexpr uint8_t aload_3 = 0x2d;
inline constexpr uint8_t anewarray = 0xbd;
inline constexpr uint8_t areturn = 0xb0;
inline constexpr uint8_t arraylength = 0xbe;
inline constexpr uint8_t astore = 0x3a;
inline constexpr uint8_t astore_0 = 0x4b;
inline constexpr uint8_t astore_1 = 0x4c;
inline constexpr uint8_t astore_2 = 0x4d;
inline constexpr uint8_t astore_3 = 0x4e;
inline constexpr uint8_t athrow = 0xbf;
inline constexpr uint8_t baload = 0x33;
inline constexpr uint8_t bastore = 0x54;
inline constexpr uint8_t bipush = 0x10;
inline constexpr uint8_t caload = 0x34;
inline constexpr uint8_t castore = 0x55;
inline constexpr uint8_t checkcast = 0xc0;
inline constexpr uint8_t d2f = 0x90;
inline constexpr uint8_t d2i = 0x8e;
inline constexpr uint8_t d2l = 0x8f;
inline constexpr uint8_t dadd = 0x63;
inline constexpr uint8_t daload = 0x31;
inline constexpr uint8_t dastore = 0x52;
inline constexpr uint8_t dcmpg = 0x98;
inline constexpr uint8_t dcmpl = 0x97;
inline constexpr uint8_t dconst_0 = 0x0e;
inline constexpr uint8_t dconst_1 = 0x0f;
inline constexpr uint8_t ddiv = 0x6f;
inline constexpr uint8_t dload = 0x18;
inline constexpr uint8_t dload_0 = 0x26;
inline constexpr uint8_t dload_1 = 0x27;
inline constexpr uint8_t dload_2 = 0x28;
inline constexpr uint8_t dload_3 = 0x29;
inline constexpr uint8_t dmul = 0x6b;
inline constexpr uint8_t dneg = 0x77;
inline constexpr uint8_t drem = 0x73;
inline constexpr uint8_t dreturn = 0xaf;
inline constexpr uint8_t dstore = 0x39;
inline constexpr uint8_t dstore_0 = 0x47;
inline constexpr uint8_t dstore_1 = 0x48;
inline constexpr uint8_t dstore_2 = 0x49;
inline constexpr uint8_t dstore_3 = 0x4a;
inline constexpr uint8_t dsub = 0x67;
inline constexpr uint8_t dup = 0x59;
inline constexpr uint8_t dup_x1 = 0x5a;
inline constexpr uint8_t dup_x2 = 0x5b;
inline constexpr uint8_t dup2 = 0x5c;
inline constexpr uint8_t dup2_x1 = 0x5d;
inline constexpr uint8_t dup2_x2 = 0x5e;
inline constexpr uint8_t f2d = 0x8d;
inline constexpr uint8_t f2i = 0x8b;
inline constexpr uint8_t f2l = 0x8c;
inline constexpr uint8_t fadd = 0x62;
inline constexpr uint8_t faload = 0x30;
inline constexpr uint8_t fastore = 0x51;
inline constexpr uint8_t fcmpg = 0x96;
inline constexpr uint8_t fcmpl = 0x95;
inline constexpr uint8_t fconst_0 = 0x0b;
inline constexpr uint8_t fconst_1 = 0x0c;
inline constexpr uint8_t fconst_2 = 0x0d;
inline constexpr uint8_t fdiv = 0x6e;
inline constexpr uint8_t fload = 0x17;
inline constexpr uint8_t fload_0 = 0x22;
inline constexpr uint8_t fload_1 = 0x23;
inline constexpr uint8_t fload_2 = 0x24;
inline constexpr uint8_t fload_3 = 0x25;
inline constexpr uint8_t fmul = 0x6a;
inline constexpr uint8_t fneg = 0x76;
inline constexpr uint8_t frem = 0x72;
inline constexpr uint8_t freturn = 0xae;
inline constexpr uint8_t fstore = 0x38;
inline constexpr uint8_t fstore_0 = 0x43;
inline constexpr uint8_t fstore_1 = 0x44;
inline constexpr uint8_t fstore_2 = 0x45;
inline constexpr uint8_t fstore_3 = 0x46;
inline constexpr uint8_t fsub = 0x66;
inline constexpr uint8_t getfield = 0xb4;
inline constexpr uint8_t getstatic = 0xb2;
inline constexpr uint8_t goto_ = 0xa7;
inline constexpr uint8_t goto_w = 0xc8;
inline constexpr uint8_t i2b = 0x91;
inline constexpr uint8_t i2c = 0x92;
inline constexpr uint8_t i2d = 0x87;
inline constexpr uint8_t i2f = 0x86;
inline constexpr uint8_t i2l = 0x85;
inline constexpr uint8_t i2s = 0x93;
inline constexpr uint8_t iadd = 0x60;
inline constexpr uint8_t iaload = 0x2e;
inline constexpr uint8_t iand = 0x7e;
inline constexpr uint8_t iastore = 0x4f;
inline constexpr uint8_t iconst_m1 = 0x02;
inline constexpr uint8_t iconst_0 = 0x03;
inline constexpr uint8_t iconst_1 = 0x04;
inline constexpr uint8_t iconst_2 = 0x05;
inline constexpr uint8_t iconst_3 = 0x06;
inline constexpr uint8_t iconst_4 = 0x07;
inline constexpr uint8_t iconst_5 = 0x08;
inline constexpr uint8_t idiv = 0x6c;
inline constexpr uint8_t if_acmpeq = 0xa5;
inline constexpr uint8_t if_acmpne = 0xa6;
inline constexpr uint8_t if_icmpeq = 0x9f;
inline constexpr uint8_t if_icmpne = 0xa0;
inline constexpr uint8_t if_icmplt = 0xa1;
inline constexpr uint8_t if_icmpge = 0xa2;
inline constexpr uint8_t if_icmpgt = 0xa3;
inline constexpr uint8_t if_icmple = 0xa4;
inline constexpr uint8_t ifeq = 0x99;
inline constexpr uint8_t ifne = 0x9a;
inline constexpr uint8_t iflt = 0x9b;
inline constexpr uint8_t ifge = 0x9c;
inline constexpr uint8_t ifgt = 0x9d;
inline constexpr uint8_t ifle = 0x9e;
inline constexpr uint8_t ifnonnull = 0xc7;
inline constexpr uint8_t ifnull = 0xc6;
inline constexpr uint8_t iinc = 0x84;
inline constexpr uint8_t iload = 0x15;
inline constexpr uint8_t iload_0 = 0x1a;
inline constexpr uint8_t iload_1 = 0x1b;
inline constexpr uint8_t iload_2 = 0x1c;
inline constexpr uint8_t iload_3 = 0x1d;
inline constexpr uint8_t imul = 0x68;
inline constexpr uint8_t ineg = 0x74;
inline constexpr uint8_t instanceof = 0xc1;
inline constexpr uint8_t invokedynamic = 0xba;
inline constexpr uint8_t invokeinterface = 0xb9;
inline constexpr uint8_t invokespecial = 0xb7;
inline constexpr uint8_t invokestatic = 0xb8;
inline constexpr uint8_t invokevirtual = 0xb6;
inline constexpr uint8_t ior = 0x80;
inline constexpr uint8_t irem = 0x70;
inline constexpr uint8_t ireturn = 0xac;
inline constexpr uint8_t ishl = 0x78;
inline constexpr uint8_t ishr = 0x7a;
inline constexpr uint8_t istore = 0x36;
inline constexpr uint8_t istore_0 = 0x3b;
inline constexpr uint8_t istore_1 = 0x3c;
inline constexpr uint8_t istore_2 = 0x3d;
inline constexpr uint8_t istore_3 = 0x3e;
inline constexpr uint8_t isub = 0x64;
inline constexpr uint8_t iushr = 0x7c;
inline constexpr uint8_t ixor = 0x82;
inline constexpr uint8_t jsr = 0xa8;
inline constexpr uint8_t jsr_w = 0xc9;
inline constexpr uint8_t l2d = 0x8a;
inline constexpr uint8_t l2f = 0x89;
inline constexpr uint8_t l2i = 0x88;
inline constexpr uint8_t ladd = 0x61;
inline constexpr uint8_t laload = 0x2f;
inline constexpr uint8_t land = 0x7f;
inline constexpr uint8_t lastore = 0x50;
inline constexpr uint8_t lcmp = 0x94;
inline constexpr uint8_t lconst_0 = 0x09;
inline constexpr uint8_t lconst_1 = 0x0a;
inline constexpr uint8_t ldc = 0x12;
inline constexpr uint8_t ldc_w = 0x13;
inline constexpr uint8_t ldc2_w = 0x14;
inline constexpr uint8_t ldiv = 0x6d;
inline constexpr uint8_t lload = 0x16;
inline constexpr uint8_t lload_0 = 0x1e;
inline constexpr uint8_t lload_1 = 0x1f;
inline constexpr uint8_t lload_2 = 0x20;
inline constexpr uint8_t lload_3 = 0x21;
inline constexpr uint8_t lmul = 0x69;
inline constexpr uint8_t lneg = 0x75;
inline constexpr uint8_t lookupswitch = 0xab;
inline constexpr uint8_t lor = 0x81;
inline constexpr uint8_t lrem = 0x71;
inline constexpr uint8_t lreturn = 0xad;
inline constexpr uint8_t lshl = 0x79;
inline constexpr uint8_t lshr = 0x7b;
inline constexpr uint8_t lstore = 0x37;
inline constexpr uint8_t lstore_0 = 0x3f;
inline constexpr uint8_t lstore_1 = 0x40;
inline constexpr uint8_t lstore_2 = 0x41;
inline constexpr uint8_t lstore_3 = 0x42;
inline constexpr uint8_t lsub = 0x65;
inline constexpr uint8_t lushr = 0x7d;
inline constexpr uint8_t lxor = 0x83;
inline constexpr uint8_t monitorenter = 0xc2;
inline constexpr uint8_t monitorexit = 0xc3;
inline constexpr uint8_t multianewarray = 0xc5;
inline constexpr uint8_t new_ = 0xbb;
inline constexpr uint8_t newarray = 0xbc;
inline constexpr uint8_t nop = 0x00;
inline constexpr uint8_t pop = 0x57;
inline constexpr uint8_t pop2 = 0x58;
inline constexpr uint8_t putfield = 0xb5;
inline constexpr uint8_t putstatic = 0xb3;
inline constexpr uint8_t ret = 0xa9;
inline constexpr uint8_t return_ = 0xb1;
inline constexpr uint8_t saload = 0x35;
inline constexpr uint8_t sastore = 0x56;
inline constexpr uint8_t sipush = 0x11;
inline constexpr uint8_t swap = 0x5f;
inline constexpr uint8_t tableswitch = 0xaa;
inline constexpr uint8_t wide = 0xc4;
} // namespace op

std::string opcode_name(int16_t opcode);

namespace array {
inline constexpr uint8_t t_boolean = 4;
inline constexpr uint8_t t_char = 5;
inline constexpr uint8_t t_float = 6;
inline constexpr uint8_t t_double = 7;
inline constexpr uint8_t t_byte = 8;
inline constexpr uint8_t t_short = 9;
inline constexpr uint8_t t_int = 10;
inline constexpr uint8_t t_long = 11;
} // namespace array
std::string array_name(uint8_t type);

} // namespace cafe
