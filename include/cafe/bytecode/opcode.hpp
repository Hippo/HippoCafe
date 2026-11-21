/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <cstdint>

namespace cafe {

/**
 * @brief JVM opcode byte values, including wire-only forms and reserved opcodes. Use opcode_info()
 * for normalization, operand shape, and version legality.
 * @ingroup bytecode
 */
enum class Opcode : std::uint8_t {
  Nop = 0x00, ///< JVM `nop` opcode; see opcode_info() for its form and legality.
  AConstNull = 0x01, ///< JVM `aconst_null` opcode; see opcode_info() for its form and legality.
  IConstM1 = 0x02, ///< JVM `iconst_m1` opcode; see opcode_info() for its form and legality.
  IConst0 = 0x03, ///< JVM `iconst_0` opcode; see opcode_info() for its form and legality.
  IConst1 = 0x04, ///< JVM `iconst_1` opcode; see opcode_info() for its form and legality.
  IConst2 = 0x05, ///< JVM `iconst_2` opcode; see opcode_info() for its form and legality.
  IConst3 = 0x06, ///< JVM `iconst_3` opcode; see opcode_info() for its form and legality.
  IConst4 = 0x07, ///< JVM `iconst_4` opcode; see opcode_info() for its form and legality.
  IConst5 = 0x08, ///< JVM `iconst_5` opcode; see opcode_info() for its form and legality.
  LConst0 = 0x09, ///< JVM `lconst_0` opcode; see opcode_info() for its form and legality.
  LConst1 = 0x0a, ///< JVM `lconst_1` opcode; see opcode_info() for its form and legality.
  FConst0 = 0x0b, ///< JVM `fconst_0` opcode; see opcode_info() for its form and legality.
  FConst1 = 0x0c, ///< JVM `fconst_1` opcode; see opcode_info() for its form and legality.
  FConst2 = 0x0d, ///< JVM `fconst_2` opcode; see opcode_info() for its form and legality.
  DConst0 = 0x0e, ///< JVM `dconst_0` opcode; see opcode_info() for its form and legality.
  DConst1 = 0x0f, ///< JVM `dconst_1` opcode; see opcode_info() for its form and legality.
  Bipush = 0x10, ///< JVM `bipush` opcode; see opcode_info() for its form and legality.
  Sipush = 0x11, ///< JVM `sipush` opcode; see opcode_info() for its form and legality.
  Ldc = 0x12, ///< JVM `ldc` opcode; see opcode_info() for its form and legality.
  LdcW = 0x13, ///< JVM `ldc_w` opcode; see opcode_info() for its form and legality.
  Ldc2W = 0x14, ///< JVM `ldc2_w` opcode; see opcode_info() for its form and legality.
  ILoad = 0x15, ///< JVM `iload` opcode; see opcode_info() for its form and legality.
  LLoad = 0x16, ///< JVM `lload` opcode; see opcode_info() for its form and legality.
  FLoad = 0x17, ///< JVM `fload` opcode; see opcode_info() for its form and legality.
  DLoad = 0x18, ///< JVM `dload` opcode; see opcode_info() for its form and legality.
  ALoad = 0x19, ///< JVM `aload` opcode; see opcode_info() for its form and legality.
  ILoad0 = 0x1a, ///< JVM `iload_0` opcode; see opcode_info() for its form and legality.
  ILoad1 = 0x1b, ///< JVM `iload_1` opcode; see opcode_info() for its form and legality.
  ILoad2 = 0x1c, ///< JVM `iload_2` opcode; see opcode_info() for its form and legality.
  ILoad3 = 0x1d, ///< JVM `iload_3` opcode; see opcode_info() for its form and legality.
  LLoad0 = 0x1e, ///< JVM `lload_0` opcode; see opcode_info() for its form and legality.
  LLoad1 = 0x1f, ///< JVM `lload_1` opcode; see opcode_info() for its form and legality.
  LLoad2 = 0x20, ///< JVM `lload_2` opcode; see opcode_info() for its form and legality.
  LLoad3 = 0x21, ///< JVM `lload_3` opcode; see opcode_info() for its form and legality.
  FLoad0 = 0x22, ///< JVM `fload_0` opcode; see opcode_info() for its form and legality.
  FLoad1 = 0x23, ///< JVM `fload_1` opcode; see opcode_info() for its form and legality.
  FLoad2 = 0x24, ///< JVM `fload_2` opcode; see opcode_info() for its form and legality.
  FLoad3 = 0x25, ///< JVM `fload_3` opcode; see opcode_info() for its form and legality.
  DLoad0 = 0x26, ///< JVM `dload_0` opcode; see opcode_info() for its form and legality.
  DLoad1 = 0x27, ///< JVM `dload_1` opcode; see opcode_info() for its form and legality.
  DLoad2 = 0x28, ///< JVM `dload_2` opcode; see opcode_info() for its form and legality.
  DLoad3 = 0x29, ///< JVM `dload_3` opcode; see opcode_info() for its form and legality.
  ALoad0 = 0x2a, ///< JVM `aload_0` opcode; see opcode_info() for its form and legality.
  ALoad1 = 0x2b, ///< JVM `aload_1` opcode; see opcode_info() for its form and legality.
  ALoad2 = 0x2c, ///< JVM `aload_2` opcode; see opcode_info() for its form and legality.
  ALoad3 = 0x2d, ///< JVM `aload_3` opcode; see opcode_info() for its form and legality.
  IALoad = 0x2e, ///< JVM `iaload` opcode; see opcode_info() for its form and legality.
  LALoad = 0x2f, ///< JVM `laload` opcode; see opcode_info() for its form and legality.
  FALoad = 0x30, ///< JVM `faload` opcode; see opcode_info() for its form and legality.
  DALoad = 0x31, ///< JVM `daload` opcode; see opcode_info() for its form and legality.
  AALoad = 0x32, ///< JVM `aaload` opcode; see opcode_info() for its form and legality.
  BALoad = 0x33, ///< JVM `baload` opcode; see opcode_info() for its form and legality.
  CALoad = 0x34, ///< JVM `caload` opcode; see opcode_info() for its form and legality.
  SALoad = 0x35, ///< JVM `saload` opcode; see opcode_info() for its form and legality.
  IStore = 0x36, ///< JVM `istore` opcode; see opcode_info() for its form and legality.
  LStore = 0x37, ///< JVM `lstore` opcode; see opcode_info() for its form and legality.
  FStore = 0x38, ///< JVM `fstore` opcode; see opcode_info() for its form and legality.
  DStore = 0x39, ///< JVM `dstore` opcode; see opcode_info() for its form and legality.
  AStore = 0x3a, ///< JVM `astore` opcode; see opcode_info() for its form and legality.
  IStore0 = 0x3b, ///< JVM `istore_0` opcode; see opcode_info() for its form and legality.
  IStore1 = 0x3c, ///< JVM `istore_1` opcode; see opcode_info() for its form and legality.
  IStore2 = 0x3d, ///< JVM `istore_2` opcode; see opcode_info() for its form and legality.
  IStore3 = 0x3e, ///< JVM `istore_3` opcode; see opcode_info() for its form and legality.
  LStore0 = 0x3f, ///< JVM `lstore_0` opcode; see opcode_info() for its form and legality.
  LStore1 = 0x40, ///< JVM `lstore_1` opcode; see opcode_info() for its form and legality.
  LStore2 = 0x41, ///< JVM `lstore_2` opcode; see opcode_info() for its form and legality.
  LStore3 = 0x42, ///< JVM `lstore_3` opcode; see opcode_info() for its form and legality.
  FStore0 = 0x43, ///< JVM `fstore_0` opcode; see opcode_info() for its form and legality.
  FStore1 = 0x44, ///< JVM `fstore_1` opcode; see opcode_info() for its form and legality.
  FStore2 = 0x45, ///< JVM `fstore_2` opcode; see opcode_info() for its form and legality.
  FStore3 = 0x46, ///< JVM `fstore_3` opcode; see opcode_info() for its form and legality.
  DStore0 = 0x47, ///< JVM `dstore_0` opcode; see opcode_info() for its form and legality.
  DStore1 = 0x48, ///< JVM `dstore_1` opcode; see opcode_info() for its form and legality.
  DStore2 = 0x49, ///< JVM `dstore_2` opcode; see opcode_info() for its form and legality.
  DStore3 = 0x4a, ///< JVM `dstore_3` opcode; see opcode_info() for its form and legality.
  AStore0 = 0x4b, ///< JVM `astore_0` opcode; see opcode_info() for its form and legality.
  AStore1 = 0x4c, ///< JVM `astore_1` opcode; see opcode_info() for its form and legality.
  AStore2 = 0x4d, ///< JVM `astore_2` opcode; see opcode_info() for its form and legality.
  AStore3 = 0x4e, ///< JVM `astore_3` opcode; see opcode_info() for its form and legality.
  IAStore = 0x4f, ///< JVM `iastore` opcode; see opcode_info() for its form and legality.
  LAStore = 0x50, ///< JVM `lastore` opcode; see opcode_info() for its form and legality.
  FAStore = 0x51, ///< JVM `fastore` opcode; see opcode_info() for its form and legality.
  DAStore = 0x52, ///< JVM `dastore` opcode; see opcode_info() for its form and legality.
  AAStore = 0x53, ///< JVM `aastore` opcode; see opcode_info() for its form and legality.
  BAStore = 0x54, ///< JVM `bastore` opcode; see opcode_info() for its form and legality.
  CAStore = 0x55, ///< JVM `castore` opcode; see opcode_info() for its form and legality.
  SAStore = 0x56, ///< JVM `sastore` opcode; see opcode_info() for its form and legality.
  Pop = 0x57, ///< JVM `pop` opcode; see opcode_info() for its form and legality.
  Pop2 = 0x58, ///< JVM `pop2` opcode; see opcode_info() for its form and legality.
  Dup = 0x59, ///< JVM `dup` opcode; see opcode_info() for its form and legality.
  DupX1 = 0x5a, ///< JVM `dup_x1` opcode; see opcode_info() for its form and legality.
  DupX2 = 0x5b, ///< JVM `dup_x2` opcode; see opcode_info() for its form and legality.
  Dup2 = 0x5c, ///< JVM `dup2` opcode; see opcode_info() for its form and legality.
  Dup2X1 = 0x5d, ///< JVM `dup2_x1` opcode; see opcode_info() for its form and legality.
  Dup2X2 = 0x5e, ///< JVM `dup2_x2` opcode; see opcode_info() for its form and legality.
  Swap = 0x5f, ///< JVM `swap` opcode; see opcode_info() for its form and legality.
  IAdd = 0x60, ///< JVM `iadd` opcode; see opcode_info() for its form and legality.
  LAdd = 0x61, ///< JVM `ladd` opcode; see opcode_info() for its form and legality.
  FAdd = 0x62, ///< JVM `fadd` opcode; see opcode_info() for its form and legality.
  DAdd = 0x63, ///< JVM `dadd` opcode; see opcode_info() for its form and legality.
  ISub = 0x64, ///< JVM `isub` opcode; see opcode_info() for its form and legality.
  LSub = 0x65, ///< JVM `lsub` opcode; see opcode_info() for its form and legality.
  FSub = 0x66, ///< JVM `fsub` opcode; see opcode_info() for its form and legality.
  DSub = 0x67, ///< JVM `dsub` opcode; see opcode_info() for its form and legality.
  IMul = 0x68, ///< JVM `imul` opcode; see opcode_info() for its form and legality.
  LMul = 0x69, ///< JVM `lmul` opcode; see opcode_info() for its form and legality.
  FMul = 0x6a, ///< JVM `fmul` opcode; see opcode_info() for its form and legality.
  DMul = 0x6b, ///< JVM `dmul` opcode; see opcode_info() for its form and legality.
  IDiv = 0x6c, ///< JVM `idiv` opcode; see opcode_info() for its form and legality.
  LDiv = 0x6d, ///< JVM `ldiv` opcode; see opcode_info() for its form and legality.
  FDiv = 0x6e, ///< JVM `fdiv` opcode; see opcode_info() for its form and legality.
  DDiv = 0x6f, ///< JVM `ddiv` opcode; see opcode_info() for its form and legality.
  IRem = 0x70, ///< JVM `irem` opcode; see opcode_info() for its form and legality.
  LRem = 0x71, ///< JVM `lrem` opcode; see opcode_info() for its form and legality.
  FRem = 0x72, ///< JVM `frem` opcode; see opcode_info() for its form and legality.
  DRem = 0x73, ///< JVM `drem` opcode; see opcode_info() for its form and legality.
  INeg = 0x74, ///< JVM `ineg` opcode; see opcode_info() for its form and legality.
  LNeg = 0x75, ///< JVM `lneg` opcode; see opcode_info() for its form and legality.
  FNeg = 0x76, ///< JVM `fneg` opcode; see opcode_info() for its form and legality.
  DNeg = 0x77, ///< JVM `dneg` opcode; see opcode_info() for its form and legality.
  IShl = 0x78, ///< JVM `ishl` opcode; see opcode_info() for its form and legality.
  LShl = 0x79, ///< JVM `lshl` opcode; see opcode_info() for its form and legality.
  IShr = 0x7a, ///< JVM `ishr` opcode; see opcode_info() for its form and legality.
  LShr = 0x7b, ///< JVM `lshr` opcode; see opcode_info() for its form and legality.
  IUShr = 0x7c, ///< JVM `iushr` opcode; see opcode_info() for its form and legality.
  LUShr = 0x7d, ///< JVM `lushr` opcode; see opcode_info() for its form and legality.
  IAnd = 0x7e, ///< JVM `iand` opcode; see opcode_info() for its form and legality.
  LAnd = 0x7f, ///< JVM `land` opcode; see opcode_info() for its form and legality.
  IOr = 0x80, ///< JVM `ior` opcode; see opcode_info() for its form and legality.
  LOr = 0x81, ///< JVM `lor` opcode; see opcode_info() for its form and legality.
  IXor = 0x82, ///< JVM `ixor` opcode; see opcode_info() for its form and legality.
  LXor = 0x83, ///< JVM `lxor` opcode; see opcode_info() for its form and legality.
  IInc = 0x84, ///< JVM `iinc` opcode; see opcode_info() for its form and legality.
  I2L = 0x85, ///< JVM `i2l` opcode; see opcode_info() for its form and legality.
  I2F = 0x86, ///< JVM `i2f` opcode; see opcode_info() for its form and legality.
  I2D = 0x87, ///< JVM `i2d` opcode; see opcode_info() for its form and legality.
  L2I = 0x88, ///< JVM `l2i` opcode; see opcode_info() for its form and legality.
  L2F = 0x89, ///< JVM `l2f` opcode; see opcode_info() for its form and legality.
  L2D = 0x8a, ///< JVM `l2d` opcode; see opcode_info() for its form and legality.
  F2I = 0x8b, ///< JVM `f2i` opcode; see opcode_info() for its form and legality.
  F2L = 0x8c, ///< JVM `f2l` opcode; see opcode_info() for its form and legality.
  F2D = 0x8d, ///< JVM `f2d` opcode; see opcode_info() for its form and legality.
  D2I = 0x8e, ///< JVM `d2i` opcode; see opcode_info() for its form and legality.
  D2L = 0x8f, ///< JVM `d2l` opcode; see opcode_info() for its form and legality.
  D2F = 0x90, ///< JVM `d2f` opcode; see opcode_info() for its form and legality.
  I2B = 0x91, ///< JVM `i2b` opcode; see opcode_info() for its form and legality.
  I2C = 0x92, ///< JVM `i2c` opcode; see opcode_info() for its form and legality.
  I2S = 0x93, ///< JVM `i2s` opcode; see opcode_info() for its form and legality.
  LCmp = 0x94, ///< JVM `lcmp` opcode; see opcode_info() for its form and legality.
  FCmpL = 0x95, ///< JVM `fcmpl` opcode; see opcode_info() for its form and legality.
  FCmpG = 0x96, ///< JVM `fcmpg` opcode; see opcode_info() for its form and legality.
  DCmpL = 0x97, ///< JVM `dcmpl` opcode; see opcode_info() for its form and legality.
  DCmpG = 0x98, ///< JVM `dcmpg` opcode; see opcode_info() for its form and legality.
  IfEq = 0x99, ///< JVM `ifeq` opcode; see opcode_info() for its form and legality.
  IfNe = 0x9a, ///< JVM `ifne` opcode; see opcode_info() for its form and legality.
  IfLt = 0x9b, ///< JVM `iflt` opcode; see opcode_info() for its form and legality.
  IfGe = 0x9c, ///< JVM `ifge` opcode; see opcode_info() for its form and legality.
  IfGt = 0x9d, ///< JVM `ifgt` opcode; see opcode_info() for its form and legality.
  IfLe = 0x9e, ///< JVM `ifle` opcode; see opcode_info() for its form and legality.
  IfICmpEq = 0x9f, ///< JVM `if_icmpeq` opcode; see opcode_info() for its form and legality.
  IfICmpNe = 0xa0, ///< JVM `if_icmpne` opcode; see opcode_info() for its form and legality.
  IfICmpLt = 0xa1, ///< JVM `if_icmplt` opcode; see opcode_info() for its form and legality.
  IfICmpGe = 0xa2, ///< JVM `if_icmpge` opcode; see opcode_info() for its form and legality.
  IfICmpGt = 0xa3, ///< JVM `if_icmpgt` opcode; see opcode_info() for its form and legality.
  IfICmpLe = 0xa4, ///< JVM `if_icmple` opcode; see opcode_info() for its form and legality.
  IfACmpEq = 0xa5, ///< JVM `if_acmpeq` opcode; see opcode_info() for its form and legality.
  IfACmpNe = 0xa6, ///< JVM `if_acmpne` opcode; see opcode_info() for its form and legality.
  Goto = 0xa7, ///< JVM `goto` opcode; see opcode_info() for its form and legality.
  Jsr = 0xa8, ///< JVM `jsr` opcode; see opcode_info() for its form and legality.
  Ret = 0xa9, ///< JVM `ret` opcode; see opcode_info() for its form and legality.
  TableSwitch = 0xaa, ///< JVM `tableswitch` opcode; see opcode_info() for its form and legality.
  LookupSwitch = 0xab, ///< JVM `lookupswitch` opcode; see opcode_info() for its form and legality.
  IReturn = 0xac, ///< JVM `ireturn` opcode; see opcode_info() for its form and legality.
  LReturn = 0xad, ///< JVM `lreturn` opcode; see opcode_info() for its form and legality.
  FReturn = 0xae, ///< JVM `freturn` opcode; see opcode_info() for its form and legality.
  DReturn = 0xaf, ///< JVM `dreturn` opcode; see opcode_info() for its form and legality.
  AReturn = 0xb0, ///< JVM `areturn` opcode; see opcode_info() for its form and legality.
  Return = 0xb1, ///< JVM `return` opcode; see opcode_info() for its form and legality.
  GetStatic = 0xb2, ///< JVM `getstatic` opcode; see opcode_info() for its form and legality.
  PutStatic = 0xb3, ///< JVM `putstatic` opcode; see opcode_info() for its form and legality.
  GetField = 0xb4, ///< JVM `getfield` opcode; see opcode_info() for its form and legality.
  PutField = 0xb5, ///< JVM `putfield` opcode; see opcode_info() for its form and legality.
  InvokeVirtual = 0xb6, ///< JVM `invokevirtual` opcode; see opcode_info() for its form and legality.
  InvokeSpecial = 0xb7, ///< JVM `invokespecial` opcode; see opcode_info() for its form and legality.
  InvokeStatic = 0xb8, ///< JVM `invokestatic` opcode; see opcode_info() for its form and legality.
  InvokeInterface = 0xb9, ///< JVM `invokeinterface` opcode; see opcode_info() for its form and legality.
  InvokeDynamic = 0xba, ///< JVM `invokedynamic` opcode; see opcode_info() for its form and legality.
  New = 0xbb, ///< JVM `new` opcode; see opcode_info() for its form and legality.
  NewArray = 0xbc, ///< JVM `newarray` opcode; see opcode_info() for its form and legality.
  ANewArray = 0xbd, ///< JVM `anewarray` opcode; see opcode_info() for its form and legality.
  ArrayLength = 0xbe, ///< JVM `arraylength` opcode; see opcode_info() for its form and legality.
  AThrow = 0xbf, ///< JVM `athrow` opcode; see opcode_info() for its form and legality.
  CheckCast = 0xc0, ///< JVM `checkcast` opcode; see opcode_info() for its form and legality.
  InstanceOf = 0xc1, ///< JVM `instanceof` opcode; see opcode_info() for its form and legality.
  MonitorEnter = 0xc2, ///< JVM `monitorenter` opcode; see opcode_info() for its form and legality.
  MonitorExit = 0xc3, ///< JVM `monitorexit` opcode; see opcode_info() for its form and legality.
  Wide = 0xc4, ///< JVM `wide` opcode; see opcode_info() for its form and legality.
  MultiANewArray = 0xc5, ///< JVM `multianewarray` opcode; see opcode_info() for its form and legality.
  IfNull = 0xc6, ///< JVM `ifnull` opcode; see opcode_info() for its form and legality.
  IfNonNull = 0xc7, ///< JVM `ifnonnull` opcode; see opcode_info() for its form and legality.
  GotoW = 0xc8, ///< JVM `goto_w` opcode; see opcode_info() for its form and legality.
  JsrW = 0xc9, ///< JVM `jsr_w` opcode; see opcode_info() for its form and legality.
  Breakpoint = 0xca, ///< JVM `breakpoint` opcode; see opcode_info() for its form and legality.
  Impdep1 = 0xfe, ///< JVM `impdep1` opcode; see opcode_info() for its form and legality.
  Impdep2 = 0xff, ///< JVM `impdep2` opcode; see opcode_info() for its form and legality.
};

/**
 * @brief Wire operand shapes used by decoding, validation, and layout.
 * @ingroup bytecode
 */
enum class OperandForm : std::uint8_t {
  Illegal, ///< Reserved or unsupported wire encoding.
  None, ///< No recovery action or no operand, according to the enclosing enum.
  SignedByte, ///< Signed one-byte immediate operand.
  SignedShort, ///< Signed two-byte immediate operand.
  Local, ///< Explicit local-variable index.
  ImplicitLocal, ///< Local-variable index embedded in the opcode.
  Increment, ///< Local index plus signed increment.
  ConstantByte, ///< One-byte constant-pool index.
  ConstantShort, ///< Two-byte constant-pool index.
  BranchShort, ///< Signed two-byte relative branch displacement.
  BranchInt, ///< Signed four-byte relative branch displacement.
  TableSwitch, ///< Dense aligned switch encoding.
  LookupSwitch, ///< Sparse aligned signed-key switch encoding.
  InvokeInterface, ///< Interface method index, argument-slot count, and reserved byte.
  InvokeDynamic, ///< Dynamic invocation index and reserved zero bytes.
  NewArray, ///< Primitive array element atype.
  MultiANewArray, ///< Array Class index and allocation dimension count.
  WidePrefix, ///< Prefix extending local and increment operand widths.
};

/**
 * @brief Normal control-flow classification of an instruction.
 * @ingroup bytecode
 */
enum class ControlFlow : std::uint8_t {
  Next, ///< Continue with the next instruction.
  ConditionalBranch, ///< Conditional target plus fallthrough.
  Jump, ///< Unconditional branch with no fallthrough.
  SubroutineCall, ///< Legacy jsr return-address call.
  SubroutineReturn, ///< Legacy ret through a local return address.
  Switch, ///< Multiway branch with no ordinary fallthrough.
  Return, ///< Return to the caller.
  Throw, ///< Throw an exception without normal fallthrough.
};

/**
 * @brief Wire encoding and control-flow metadata for one opcode byte. Logical opcodes normalize
 * implicit local instructions and wide branch/load encodings.
 * @ingroup bytecode
 */
struct OpcodeInfo {
  Opcode logical_opcode = Opcode::Nop; ///< Normalized logical operation for the wire opcode.
  OperandForm form = OperandForm::Illegal; ///< Wire operand encoding category.
  ControlFlow control_flow = ControlFlow::Next; ///< Normal control-flow behavior of the opcode.
  /**
   * @brief Encoded byte count including opcode; zero for variable-width or illegal encodings.
   */
  std::uint8_t wire_width = 0;
  std::uint8_t implicit_local = 0; ///< Local slot encoded in the opcode for implicit local forms.
  std::uint8_t local_width = 0; ///< Local slots accessed: zero, one, or two.
  std::uint16_t minimum_major = 45; ///< First class-file major permitting this opcode.
  std::uint16_t maximum_major = UINT16_MAX; ///< Last class-file major permitting this opcode.

  /**
   * @brief Check opcode availability for a class-file major version.
   * @param major Class-file major version to check.
   * @return True when the form is legal and the major lies within its supported interval.
   */
  [[nodiscard]] constexpr bool is_allowed(std::uint16_t major) const {
    return form != OperandForm::Illegal && major >= minimum_major && major <= maximum_major;
  }
};

/**
 * @brief Look up metadata for any opcode byte, including reserved encodings.
 * @param opcode Opcode whose operand form or metadata is requested.
 * @return A process-lifetime reference; illegal encodings have OperandForm::Illegal.
 * @ingroup bytecode
 */
[[nodiscard]] const OpcodeInfo& opcode_info(Opcode opcode);

} // namespace cafe
