#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <cafe/analysis/ssa.hpp>
#include <cafe/classpath/class_hierarchy.hpp>
#include <cafe/io/class_reader.hpp>
#include <cafe/io/class_writer.hpp>

#include "analysis/instruction_effects.hpp"

namespace cafe::analysis {
namespace {

using K = VerificationKind;
using V = VerificationType;

constexpr ClassVersion MODERN{69, 0};
constexpr std::uint16_t LOCAL_COUNT = 8;
constexpr V INT{K::Integer};
constexpr V LONG{K::Long};
constexpr V FLOAT{K::Float};
constexpr V DOUBLE{K::Double};
constexpr V NULL_VALUE{K::Null};
constexpr V SELF{K::Object, "p/Owner"};
constexpr V STRING{K::Object, "java/lang/String"};
constexpr V ROOT{K::Object, "java/lang/Object"};
constexpr V EXCEPTION{K::Object, "p/Exception"};
constexpr V INT_ARRAY{K::Object, "[I"};
constexpr V LONG_ARRAY{K::Object, "[J"};
constexpr V FLOAT_ARRAY{K::Object, "[F"};
constexpr V DOUBLE_ARRAY{K::Object, "[D"};
constexpr V BYTE_ARRAY{K::Object, "[B"};
constexpr V BOOL_ARRAY{K::Object, "[Z"};
constexpr V CHAR_ARRAY{K::Object, "[C"};
constexpr V SHORT_ARRAY{K::Object, "[S"};
constexpr V STRING_ARRAY{K::Object, "[Ljava/lang/String;"};
constexpr V WIDE_CONTINUATION{K::WideContinuation};

Instruction simple(Opcode opcode) {
  return Instruction::simple(opcode).value();
}

void give_stack(Frame& frame, std::span<const V> stack) {
  frame.stack.assign(stack.begin(), stack.end());
  frame.stack_slots = 0;
  for (const auto type : stack)
    frame.stack_slots += type.width();
}

ClassFile make_owner(ClassVersion version) {
  auto owner = ClassFile::create(version, "p/Owner", "p/Parent").value();
  auto& pool = owner.constant_pool();
  const auto target = pool.append(CpEntry{CpTag::Class, 0, pool.append_utf8("p/Target").value(), {}}).value();
  owner.add_interface(target).value();
  return owner;
}

classpath::ClassHierarchy make_hierarchy(const ClassFile& owner) {
  classpath::ClassPathBuilder path;
  path.add_class(ClassFile::create(MODERN, "java/lang/Object", std::nullopt).value()).value();
  for (const auto name : {"java/lang/String", "java/lang/Throwable", "p/Parent"})
    path.add_class(ClassFile::create(MODERN, name).value()).value();
  path.add_class(ClassFile::create(MODERN, "p/Exception", "java/lang/Throwable").value()).value();
  path.add_class(ClassFile::create(MODERN, "p/Target", "java/lang/Object", 0x0601).value()).value();
  path.add_class(owner).value();
  return classpath::ClassHierarchy(classpath::ClassResolver(path.build()));
}

struct TestMethod {
  explicit TestMethod(ClassVersion version = MODERN) : owner(make_owner(version)), hierarchy(make_hierarchy(owner)) {
  }

  CpIndex member(CpTag tag, std::string_view name, std::string_view descriptor, CpIndex member_class = {}) {
    auto& pool = owner.constant_pool();
    const auto type = pool.append_utf8(descriptor).value();
    const auto name_index = pool.append_utf8(name).value();
    const auto signature = pool.append(CpEntry{CpTag::NameAndType, 0, name_index, type}).value();
    return pool.append(CpEntry{tag, 0, member_class, signature}).value();
  }

  CpIndex class_index(std::string_view name) {
    auto& pool = owner.constant_pool();
    const auto name_index = pool.append_utf8(name).value();
    return pool.append(CpEntry{CpTag::Class, 0, name_index, {}}).value();
  }

  const Method& method(std::string_view descriptor = "()V", std::string_view name = "test", std::uint16_t flags = 0x0009) {
    auto& pool = owner.constant_pool();
    const auto name_index = pool.append_utf8(name).value();
    const auto type = pool.append_utf8(descriptor).value();
    const auto index = owner.add_method(flags, name_index, type).value();
    return owner.methods()[index];
  }

  Instruction constant(Opcode opcode, CpIndex index) {
    return Instruction::constant(opcode, index).value();
  }

  ClassFile owner;
  classpath::ClassHierarchy hierarchy;
  MethodCode code;
};

template<typename Check>
void sample_opcode_shapes(TestMethod& test, const Check& check) {
  const auto branch_target = test.code.make_label().value();
  const auto branch = [branch_target](Opcode opcode) {
    return Instruction::branch(opcode, branch_target).value();
  };
  const auto sample = [&](Opcode opcode, std::initializer_list<V> before, std::initializer_list<V> after) {
    check(simple(opcode), before, after);
  };
  const auto range = [](Opcode first, Opcode last, auto fn) {
    for (auto opcode = static_cast<unsigned>(first); opcode <= static_cast<unsigned>(last); ++opcode)
      fn(static_cast<Opcode>(opcode));
  };

  sample(Opcode::Nop, {}, {});
  sample(Opcode::AConstNull, {}, {NULL_VALUE});
  range(Opcode::IConstM1, Opcode::IConst5, [&](Opcode opcode) { sample(opcode, {}, {INT}); });
  range(Opcode::LConst0, Opcode::LConst1, [&](Opcode opcode) { sample(opcode, {}, {LONG}); });
  range(Opcode::FConst0, Opcode::FConst2, [&](Opcode opcode) { sample(opcode, {}, {FLOAT}); });
  range(Opcode::DConst0, Opcode::DConst1, [&](Opcode opcode) { sample(opcode, {}, {DOUBLE}); });
  check(Instruction::bipush(-80), {}, {INT});
  check(Instruction::sipush(-1000), {}, {INT});

  auto& pool = test.owner.constant_pool();
  const auto number = pool.append(CpEntry{CpTag::Integer, 0, {}, {}}).value();
  const auto wide = pool.append(CpEntry{CpTag::Long, 0, {}, {}}).value();
  check(test.constant(Opcode::Ldc, number), {}, {INT});
  check(test.constant(Opcode::Ldc2W, wide), {}, {LONG});

  for (auto [load, store, value] : std::array{
           std::tuple{Opcode::ILoad, Opcode::IStore, INT},
           std::tuple{Opcode::LLoad, Opcode::LStore, LONG},
           std::tuple{Opcode::FLoad, Opcode::FStore, FLOAT},
           std::tuple{Opcode::DLoad, Opcode::DStore, DOUBLE},
           std::tuple{Opcode::ALoad, Opcode::AStore, SELF},
       }) {
    check(Instruction::local(load, 3).value(), {}, {value}, value);
    check(Instruction::local(store, 3).value(), {value}, {});
  }
  check(Instruction::increment(3, -50), {}, {}, INT);

  for (auto [load, store, type, array] : std::array{
           std::tuple{Opcode::IALoad, Opcode::IAStore, INT, INT_ARRAY},
           std::tuple{Opcode::LALoad, Opcode::LAStore, LONG, LONG_ARRAY},
           std::tuple{Opcode::FALoad, Opcode::FAStore, FLOAT, FLOAT_ARRAY},
           std::tuple{Opcode::DALoad, Opcode::DAStore, DOUBLE, DOUBLE_ARRAY},
           std::tuple{Opcode::AALoad, Opcode::AAStore, STRING, STRING_ARRAY},
           std::tuple{Opcode::BALoad, Opcode::BAStore, INT, BYTE_ARRAY},
           std::tuple{Opcode::CALoad, Opcode::CAStore, INT, CHAR_ARRAY},
           std::tuple{Opcode::SALoad, Opcode::SAStore, INT, SHORT_ARRAY},
       }) {
    sample(load, {array, INT}, {type});
    sample(store, {array, INT, type}, {});
  }

  sample(Opcode::Pop, {FLOAT}, {});
  sample(Opcode::Pop2, {DOUBLE}, {});
  sample(Opcode::Dup, {INT}, {INT, INT});
  sample(Opcode::DupX1, {FLOAT, INT}, {INT, FLOAT, INT});
  sample(Opcode::DupX2, {LONG, INT}, {INT, LONG, INT});
  sample(Opcode::Dup2, {DOUBLE}, {DOUBLE, DOUBLE});
  sample(Opcode::Dup2X1, {FLOAT, LONG}, {LONG, FLOAT, LONG});
  sample(Opcode::Dup2X2, {DOUBLE, LONG}, {LONG, DOUBLE, LONG});
  sample(Opcode::Swap, {FLOAT, INT}, {INT, FLOAT});

  for (auto group : {std::array{Opcode::IAdd, Opcode::LAdd, Opcode::FAdd, Opcode::DAdd},
                     std::array{Opcode::ISub, Opcode::LSub, Opcode::FSub, Opcode::DSub},
                     std::array{Opcode::IMul, Opcode::LMul, Opcode::FMul, Opcode::DMul},
                     std::array{Opcode::IDiv, Opcode::LDiv, Opcode::FDiv, Opcode::DDiv},
                     std::array{Opcode::IRem, Opcode::LRem, Opcode::FRem, Opcode::DRem}}) {
    constexpr std::array TYPES{INT, LONG, FLOAT, DOUBLE};
    for (std::size_t i = 0; i < group.size(); ++i)
      sample(group[i], {TYPES[i], TYPES[i]}, {TYPES[i]});
  }
  sample(Opcode::INeg, {INT}, {INT});
  sample(Opcode::LNeg, {LONG}, {LONG});
  sample(Opcode::FNeg, {FLOAT}, {FLOAT});
  sample(Opcode::DNeg, {DOUBLE}, {DOUBLE});
  for (auto opcode : {Opcode::IShl, Opcode::IShr, Opcode::IUShr, Opcode::IAnd, Opcode::IOr, Opcode::IXor})
    sample(opcode, {INT, INT}, {INT});
  for (auto opcode : {Opcode::LShl, Opcode::LShr, Opcode::LUShr})
    sample(opcode, {LONG, INT}, {LONG});
  for (auto opcode : {Opcode::LAnd, Opcode::LOr, Opcode::LXor})
    sample(opcode, {LONG, LONG}, {LONG});
  for (auto [opcode, from, to] : std::array{
           std::tuple{Opcode::I2L, INT, LONG},
           std::tuple{Opcode::I2F, INT, FLOAT},
           std::tuple{Opcode::I2D, INT, DOUBLE},
           std::tuple{Opcode::L2I, LONG, INT},
           std::tuple{Opcode::L2F, LONG, FLOAT},
           std::tuple{Opcode::L2D, LONG, DOUBLE},
           std::tuple{Opcode::F2I, FLOAT, INT},
           std::tuple{Opcode::F2L, FLOAT, LONG},
           std::tuple{Opcode::F2D, FLOAT, DOUBLE},
           std::tuple{Opcode::D2I, DOUBLE, INT},
           std::tuple{Opcode::D2L, DOUBLE, LONG},
           std::tuple{Opcode::D2F, DOUBLE, FLOAT},
           std::tuple{Opcode::I2B, INT, INT},
           std::tuple{Opcode::I2C, INT, INT},
           std::tuple{Opcode::I2S, INT, INT},
       })
    sample(opcode, {from}, {to});
  sample(Opcode::LCmp, {LONG, LONG}, {INT});
  for (auto opcode : {Opcode::FCmpL, Opcode::FCmpG})
    sample(opcode, {FLOAT, FLOAT}, {INT});
  for (auto opcode : {Opcode::DCmpL, Opcode::DCmpG})
    sample(opcode, {DOUBLE, DOUBLE}, {INT});

  range(Opcode::IfEq, Opcode::IfLe, [&](Opcode opcode) { check(branch(opcode), {INT}, {}); });
  range(Opcode::IfICmpEq, Opcode::IfICmpLe, [&](Opcode opcode) { check(branch(opcode), {INT, INT}, {}); });
  for (auto opcode : {Opcode::IfACmpEq, Opcode::IfACmpNe})
    check(branch(opcode), {SELF, NULL_VALUE}, {});
  for (auto opcode : {Opcode::IfNull, Opcode::IfNonNull})
    check(branch(opcode), {SELF}, {});
  check(branch(Opcode::Goto), {}, {});
  const std::array cases{branch_target};
  check(TableSwitch{0, branch_target, cases}, {INT}, {});
  check(LookupSwitch{branch_target, {}}, {INT}, {});

  sample(Opcode::Return, {LONG, INT}, {});
  const auto parent_class = test.owner.super_class();
  const auto field = test.member(CpTag::Fieldref, "field", "J", parent_class);
  check(test.constant(Opcode::GetStatic, field), {}, {LONG});
  check(test.constant(Opcode::PutStatic, field), {LONG}, {});
  check(test.constant(Opcode::GetField, field), {SELF}, {LONG});
  check(test.constant(Opcode::PutField, field), {SELF, LONG}, {});
  const auto call = test.member(CpTag::Methodref, "method", "(JD)I", parent_class);
  check(test.constant(Opcode::InvokeVirtual, call), {SELF, LONG, DOUBLE}, {INT});
  check(test.constant(Opcode::InvokeSpecial, call), {SELF, LONG, DOUBLE}, {INT});
  check(test.constant(Opcode::InvokeStatic, call), {LONG, DOUBLE}, {INT});
  const auto interface_call = test.member(CpTag::InterfaceMethodref, "method", "(JD)I", test.owner.interfaces().front());
  check(Instruction::invoke_interface(interface_call, 5).value(), {SELF, LONG, DOUBLE}, {INT});
  const auto dynamic_call = test.member(CpTag::InvokeDynamic, "method", "(JD)I");
  check(test.constant(Opcode::InvokeDynamic, dynamic_call), {LONG, DOUBLE}, {INT});

  check(Instruction::new_array(ArrayType::Int).value(), {INT}, {INT_ARRAY});
  const auto string_class = test.class_index("java/lang/String");
  check(test.constant(Opcode::ANewArray, string_class), {INT}, {STRING_ARRAY});
  const auto matrix_class = test.class_index("[[D");
  check(Instruction::multi_array(matrix_class, 2).value(), {INT, INT}, {V{K::Object, "[[D"}});
  sample(Opcode::ArrayLength, {STRING_ARRAY}, {INT});
  sample(Opcode::AThrow, {EXCEPTION}, {});
  check(test.constant(Opcode::CheckCast, string_class), {ROOT}, {STRING});
  check(test.constant(Opcode::InstanceOf, string_class), {ROOT}, {INT});
  sample(Opcode::MonitorEnter, {SELF}, {});
  sample(Opcode::MonitorExit, {SELF}, {});
}

TEST(InstructionEffects, AllLogicalOpcodesHaveAnExplicitOutcome) {
  TestMethod test;
  std::array<bool, 256> sampled{};
  std::pmr::unsynchronized_pool_resource scratch;
  const auto& method = test.method();
  InstructionEffects effects(test.owner, method, &test.hierarchy, &scratch);
  const auto branch_target = test.code.make_label().value();
  const auto branch = [branch_target](Opcode opcode) {
    return Instruction::branch(opcode, branch_target).value();
  };
  const auto check = [&](const InstructionBuilder& builder, std::initializer_list<V> before,
                         std::initializer_list<V> after, std::optional<V> argument_local = std::nullopt) {
    const auto at = test.code.append(builder).value();
    const auto instruction = test.code.get(at).value();
    const auto opcode = instruction.opcode();
    SCOPED_TRACE(static_cast<unsigned>(opcode));
    sampled[static_cast<unsigned>(opcode)] = true;
    Frame frame(&scratch);
    ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
    if (argument_local) {
      frame.locals[3] = *argument_local;
      if (argument_local->width() == 2)
        frame.locals[4] = WIDE_CONTINUATION;
    }
    give_stack(frame, before);
    const auto prepared = effects.prepare(at, instruction, LOCAL_COUNT);
    ASSERT_TRUE(prepared) << prepared.error().detail;
    InstructionTransfer transfer;
    const auto applied = effects.apply(frame, at, instruction, &transfer);
    ASSERT_TRUE(applied) << applied.error().detail;
    EXPECT_TRUE(std::ranges::equal(frame.stack, after));
    EXPECT_EQ(transfer.popped, opcode == Opcode::Return ? 0u : before.size());
    if (opcode >= Opcode::Pop && opcode <= Opcode::Swap) {
      ASSERT_TRUE(transfer.reordered);
      ASSERT_EQ(transfer.stack_map_size, after.size());
      for (std::size_t i = 0; i < after.size(); ++i) {
        ASSERT_LT(transfer.stack_map[i], before.size());
        EXPECT_EQ(before.begin()[transfer.stack_map[i]], after.begin()[i]);
      }
    } else {
      EXPECT_FALSE(transfer.reordered);
      EXPECT_EQ(transfer.produced, after.size() == 1);
    }
    unsigned words = 0;
    for (auto item : after)
      words += item.width();
    EXPECT_EQ(frame.stack_slots, words);
  };
  sample_opcode_shapes(test, check);
  const auto allocation = test.constant(Opcode::New, test.owner.this_class());
  const auto site = test.code.append(allocation).value();
  sampled[static_cast<unsigned>(Opcode::New)] = true;
  {
    Frame frame(&scratch);
    ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
    ASSERT_TRUE(effects.prepare(site, allocation, LOCAL_COUNT));
    ASSERT_TRUE(effects.apply(frame, site, allocation));
    ASSERT_EQ(frame.stack.size(), 1u);
    EXPECT_EQ(frame.stack.back(), (V{K::Uninitialized, SELF.reference, site}));
  }
  for (const auto& builder : {InstructionBuilder{branch(Opcode::Jsr)},
                              InstructionBuilder{Instruction::local(Opcode::Ret, 1).value()}}) {
    const auto at = test.code.append(builder).value();
    const auto instruction = test.code.get(at).value();
    sampled[static_cast<unsigned>(instruction.opcode())] = true;
    const auto status = effects.prepare(at, instruction, LOCAL_COUNT);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().kind, AnalysisErrorKind::UnsupportedSubroutineAnalysis);
  }
  // The JVM discards any operands below the return value. They are
  // immaterial to the normal state of this terminal instruction.
  for (auto [opcode, descriptor, result] : std::array{
           std::tuple{Opcode::IReturn, "()I", INT},
           std::tuple{Opcode::LReturn, "()J", LONG},
           std::tuple{Opcode::FReturn, "()F", FLOAT},
           std::tuple{Opcode::DReturn, "()D", DOUBLE},
           std::tuple{Opcode::AReturn, "()Ljava/lang/Object;", ROOT},
       }) {
    sampled[static_cast<unsigned>(opcode)] = true;
    const auto& returning = test.method(descriptor);
    InstructionEffects transfer(test.owner, returning, &test.hierarchy, &scratch);
    Frame frame(&scratch);
    ASSERT_TRUE(transfer.initialize(frame, LOCAL_COUNT));
    const std::array input{DOUBLE, result};
    give_stack(frame, input);
    ASSERT_TRUE(transfer.prepare({}, simple(opcode), LOCAL_COUNT));
    ASSERT_TRUE(transfer.apply(frame, {}, simple(opcode)));
  }

  for (unsigned code = 0; code < sampled.size(); ++code) {
    const auto opcode = static_cast<Opcode>(code);
    const auto& metadata = opcode_info(opcode);
    if (metadata.form != OperandForm::Illegal && metadata.form != OperandForm::WidePrefix &&
        metadata.logical_opcode == opcode) {
      EXPECT_TRUE(sampled[code]) << "logical opcode " << code;
    }
  }
}

std::string descriptor_of(V type) {
  switch (type.kind) {
    case K::Integer:
      return "I";
    case K::Long:
      return "J";
    case K::Float:
      return "F";
    case K::Double:
      return "D";
    case K::Null:
      return "Ljava/lang/Object;";
    case K::Object:
    case K::Uninitialized:
      return type.reference.starts_with('[') ? std::string(type.reference) : "L" + std::string(type.reference) + ";";
    default:
      throw std::logic_error("unusable test parameter type");
  }
}

Instruction load_value(V type, std::uint16_t slot) {
  switch (type.kind) {
    case K::Integer:
      return Instruction::iload(slot);
    case K::Long:
      return Instruction::lload(slot);
    case K::Float:
      return Instruction::fload(slot);
    case K::Double:
      return Instruction::dload(slot);
    default:
      return Instruction::aload(slot);
  }
}

// Drive the concrete SSA API and ordinary class writer with the same registry of *independent*
// operand shapes as the verifier test. A method parameter supplies each input with a different
// SSA identity, including when multiple operands have exactly the same verification type.
TEST(SsaLift, RegistryOperandsSurviveLoweringAndStrictReparse) {
  TestMethod test;
  auto& pool = test.owner.constant_pool();
  const auto code_name = pool.append_utf8("Code").value();
  const auto bootstrap = test.member(CpTag::Methodref, "bootstrap",
                                     "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/CallSite;",
                                     test.owner.super_class());
  const auto handle = pool.append({CpTag::MethodHandle, 6, bootstrap, {}}).value();
  test.owner.add_attribute(Attribute::bootstrap_methods(pool.append_utf8("BootstrapMethods").value(),
                                                        std::array{BootstrapMethod{handle, {}}}))
      .value();
  AnalysisContext context(test.hierarchy);
  std::array<bool, 256> exercised{};

  const auto check = [&](const InstructionBuilder& builder, std::initializer_list<V> before,
                         std::initializer_list<V> after, std::optional<V> local = {}) {
    const auto spec = test.code.get(test.code.append(builder).value()).value();
    const auto opcode = spec.opcode();
    SCOPED_TRACE(static_cast<unsigned>(opcode));
    const auto kind = opcode_info(opcode).control_flow;
    const bool load = opcode >= Opcode::ILoad && opcode <= Opcode::ALoad;
    const bool store = opcode >= Opcode::IStore && opcode <= Opcode::AStore;
    const bool shuffle = opcode >= Opcode::Pop && opcode <= Opcode::Swap;
    const bool typed_return = kind == ControlFlow::Return && opcode != Opcode::Return;
    std::vector<V> args(before);
    if (local)
      args.push_back(*local);
    std::string signature = "(";
    for (const auto type : args)
      signature += descriptor_of(type);
    signature += ")";
    signature += typed_return ? descriptor_of(*(before.end() - 1)) : "V";
    const auto index = test.owner.methods().size();
    test.method(signature, "op" + std::to_string(static_cast<unsigned>(opcode)));
    auto& code = test.owner.add_code(index, code_name).value().get().code();
    std::uint16_t slot = 0;
    for (const auto type : before) {
      code.append(load_value(type, slot)).value();
      slot = static_cast<std::uint16_t>(slot + type.width());
    }

    if (load || opcode == Opcode::IInc) {
      code.append(load ? Instruction::local(opcode, slot).value() : Instruction::increment(slot, spec.increment_value()->amount)).value();
    } else if (kind == ControlFlow::Jump || kind == ControlFlow::ConditionalBranch || kind == ControlFlow::Switch) {
      auto target = code.make_label().value();
      if (kind == ControlFlow::Switch) {
        if (opcode == Opcode::TableSwitch)
          code.append(TableSwitch{0, target, std::array{target}}).value();
        else
          code.append(LookupSwitch{target, {}}).value();
      } else {
        code.append(Branch{opcode, target}).value();
      }
      if (kind == ControlFlow::ConditionalBranch)
        code.append(Instruction::return_void()).value();
      const auto end = code.append(Instruction::return_void()).value();
      code.bind_label(target, {end}).value();
    } else {
      code.append(spec).value();
    }

    std::vector<V> observed(after);
    if (store || opcode == Opcode::IInc) {
      const auto type = store ? *before.begin() : *local;
      code.append(load_value(type, store ? *spec.local_index() : slot)).value();
      observed = {type};
    }
    CpIndex observer{};
    if (opcode == Opcode::New) {
      code.append(Instruction::pop()).value();
    } else if (kind == ControlFlow::Next && !observed.empty()) {
      std::string consume = "(";
      for (auto type : observed)
        consume += descriptor_of(type);
      consume += ")V";
      observer = test.member(CpTag::Methodref, "observe", consume, test.owner.super_class());
      code.append(Instruction::constant(Opcode::InvokeStatic, observer).value()).value();
    }
    if (kind == ControlFlow::Next)
      code.append(Instruction::return_void()).value();

    const auto inspect = [&](const SsaMethod& graph, bool relifted) {
      const auto expected_opcode = relifted && opcode == Opcode::IInc ? Opcode::IAdd : opcode;
      const SsaOperation* tested = nullptr;
      const SsaOperation* sink = nullptr;
      for (const auto& block : graph.blocks())
        for (const auto id : block.operations) {
          const auto* op = graph.operation(id);
          if (observer && op->instruction.constant_index() == observer)
            sink = op;
          else if (op->opcode == expected_opcode && !op->anchor)
            tested = op;
        }
      const auto params = graph.parameters();
      if (!(load || store || shuffle || (relifted && opcode == Opcode::Goto))) {
        ASSERT_NE(tested, nullptr);
        if (typed_return) {
          ASSERT_EQ(tested->inputs.size(), 1u);
          EXPECT_EQ(tested->inputs.front(), params.back());
        } else if (opcode == Opcode::Return) {
          EXPECT_TRUE(tested->inputs.empty());
        } else if (opcode == Opcode::IInc) {
          ASSERT_EQ(tested->inputs.size(), relifted ? 2u : 1u);
          EXPECT_EQ(tested->inputs.front(), params.front());
          if (relifted) {
            const auto* immediate = graph.value(tested->inputs.back());
            ASSERT_NE(immediate, nullptr);
            const auto* op = graph.operation(immediate->operation);
            ASSERT_NE(op, nullptr);
            EXPECT_EQ(op->instruction.immediate_value(), spec.increment_value()->amount);
          }
        } else {
          EXPECT_TRUE(std::ranges::equal(tested->inputs, params));
        }
        if (spec.constant_index()) {
          EXPECT_EQ(tested->instruction.constant_index(), spec.constant_index());
        }
      }
      if (observer) {
        ASSERT_NE(sink, nullptr);
        ASSERT_EQ(sink->inputs.size(), observed.size());
        for (std::size_t i = 0; i < observed.size(); ++i) {
          if (load || store) {
            EXPECT_EQ(sink->inputs[i], params.front());
          } else if (shuffle) {
            // Inputs of each registry shuffle case have distinct types; the exhaustive shape
            // test also exercises same-typed inputs in every JVM category permutation.
            const auto source = std::ranges::find(args, observed[i]);
            ASSERT_NE(source, args.end());
            EXPECT_EQ(sink->inputs[i], params[static_cast<std::size_t>(source - args.begin())]);
          } else {
            ASSERT_NE(tested, nullptr);
            EXPECT_EQ(sink->inputs[i], tested->result);
          }
        }
      }
    };
    auto graph = lift_ssa(test.owner, index, context);
    ASSERT_TRUE(graph) << graph.error().detail;
    ASSERT_NO_FATAL_FAILURE(inspect(*graph, false));
    const auto lowered = lower_ssa(test.owner, *graph, context);
    ASSERT_TRUE(lowered) << lowered.error().detail;
    const auto emitted = write_class(test.owner);
    ASSERT_TRUE(emitted) << emitted.error().detail;
    const auto parsed = read_class(emitted->bytes, {.mode = ParseMode::Strict});
    ASSERT_TRUE(parsed) << parsed.error().detail;
    auto again = lift_ssa(parsed->class_file, index, context);
    ASSERT_TRUE(again) << again.error().detail;
    ASSERT_NO_FATAL_FAILURE(inspect(*again, true));
    exercised[static_cast<unsigned>(opcode)] = true;
  };
  sample_opcode_shapes(test, check);
  check(test.constant(Opcode::New, test.owner.this_class()), {}, {V{K::Uninitialized, SELF.reference}});
  for (const auto [opcode, type] : std::array{std::pair{Opcode::IReturn, INT}, std::pair{Opcode::LReturn, LONG},
                                              std::pair{Opcode::FReturn, FLOAT}, std::pair{Opcode::DReturn, DOUBLE}, std::pair{Opcode::AReturn, ROOT}})
    check(simple(opcode), {DOUBLE, type}, {});
  for (unsigned byte = 0; byte < exercised.size(); ++byte) {
    const auto op = static_cast<Opcode>(byte);
    const auto& info = opcode_info(op);
    if (info.form != OperandForm::Illegal && info.form != OperandForm::WidePrefix &&
        info.logical_opcode == op && op != Opcode::Jsr && op != Opcode::Ret) {
      EXPECT_TRUE(exercised[byte]) << "missing SSA pipeline coverage for logical opcode " << byte;
    }
  }
}

TEST(InstructionEffects, StackManipulationValidatesAllCategoryForms) {
  TestMethod test;
  const auto& method = test.method();
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, method, nullptr, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));

  struct Form {
    Opcode opcode;
    std::string_view suffix;
    // Output order, with positions counted from the bottom of the suffix.
    std::string_view result;
  };
  constexpr std::array FORMS{
      Form{Opcode::Pop, "1", ""},
      Form{Opcode::Pop2, "11", ""},
      Form{Opcode::Pop2, "2", ""},
      Form{Opcode::Dup, "1", "00"},
      Form{Opcode::DupX1, "11", "101"},
      Form{Opcode::DupX2, "111", "2012"},
      Form{Opcode::DupX2, "21", "101"},
      Form{Opcode::Dup2, "11", "0101"},
      Form{Opcode::Dup2, "2", "00"},
      Form{Opcode::Dup2X1, "111", "12012"},
      Form{Opcode::Dup2X1, "12", "101"},
      Form{Opcode::Dup2X2, "1111", "230123"},
      Form{Opcode::Dup2X2, "112", "2012"},
      Form{Opcode::Dup2X2, "211", "12012"},
      Form{Opcode::Dup2X2, "22", "101"},
      Form{Opcode::Swap, "11", "10"},
  };
  constexpr std::array NARROW{INT, FLOAT, NULL_VALUE, SELF};
  constexpr std::array WIDE{LONG, DOUBLE};

  for (auto op = static_cast<unsigned>(Opcode::Pop); op <= static_cast<unsigned>(Opcode::Swap); ++op) {
    const auto opcode = static_cast<Opcode>(op);
    const auto instruction = simple(opcode);
    ASSERT_TRUE(effects.prepare({}, instruction, LOCAL_COUNT));
    for (unsigned values = 0; values <= 4; ++values) {
      for (unsigned mask = 0; mask < (1u << values); ++mask) {
        std::string shape;
        std::vector<V> before;
        for (unsigned i = 0; i < values; ++i) {
          const bool wide = ((mask >> i) & 1u) != 0;
          shape.push_back(wide ? '2' : '1');
          before.push_back(wide ? WIDE[i % WIDE.size()] : NARROW[i]);
        }
        SCOPED_TRACE(std::to_string(op) + " [" + shape + "]");
        give_stack(frame, before);
        const auto completed = effects.apply(frame, {}, instruction);
        const auto form = std::ranges::find_if(FORMS, [&](const Form& candidate) {
          return candidate.opcode == opcode && shape.ends_with(candidate.suffix);
        });
        if (form == FORMS.end()) {
          ASSERT_FALSE(completed);
          EXPECT_TRUE(completed.error().kind == AnalysisErrorKind::InvalidCategoryOperation ||
                      completed.error().kind == AnalysisErrorKind::StackUnderflow);
          continue;
        }
        ASSERT_TRUE(completed) << completed.error().detail;
        const auto prefix = before.size() - form->suffix.size();
        std::vector<V> after(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(prefix));
        for (const auto position : form->result)
          after.push_back(before[prefix + static_cast<std::size_t>(position - '0')]);
        EXPECT_TRUE(std::ranges::equal(frame.stack, after));
        unsigned slots = 0;
        for (auto type : after)
          slots += type.width();
        EXPECT_EQ(frame.stack_slots, slots);
      }
    }
  }
}

TEST(InstructionEffects, InitialLocalsAndOverlappingWideStoresUseJvmSlots) {
  TestMethod test;
  const auto& method = test.method("(J[DLjava/lang/String;B)V", "instance", 0x0001);
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, method, &test.hierarchy, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
  const std::array locals{SELF, LONG, WIDE_CONTINUATION, DOUBLE_ARRAY, STRING, INT, V{}, V{}};
  EXPECT_TRUE(std::ranges::equal(frame.locals, locals));
  const auto run = [&](Instruction instruction) {
    EXPECT_TRUE(effects.prepare({}, instruction, LOCAL_COUNT));
    return effects.apply(frame, {}, instruction);
  };

  const std::array first{DOUBLE};
  give_stack(frame, first);
  ASSERT_TRUE(run(Instruction::dstore(2)));
  EXPECT_EQ(frame.locals[1].kind, K::Top);
  EXPECT_EQ(frame.locals[2], DOUBLE);
  EXPECT_EQ(frame.locals[3], WIDE_CONTINUATION);
  const std::array next{LONG};
  give_stack(frame, next);
  ASSERT_TRUE(run(Instruction::lstore(1)));
  EXPECT_EQ(frame.locals[1], LONG);
  EXPECT_EQ(frame.locals[2], WIDE_CONTINUATION);
  EXPECT_EQ(frame.locals[3].kind, K::Top);
  const std::array narrow{INT};
  give_stack(frame, narrow);
  ASSERT_TRUE(run(Instruction::istore(2)));
  ASSERT_TRUE(run(Instruction::increment(2, -1)));
  EXPECT_EQ(frame.locals[1].kind, K::Top);
  EXPECT_EQ(frame.locals[2], INT);
  auto invalid = run(Instruction::lload(1));
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().kind, AnalysisErrorKind::TypeMismatch);
}

TEST(InstructionEffects, ConstructorTransitionsOnlyTheCalledReceiverIdentity) {
  TestMethod test;
  auto& pool = test.owner.constant_pool();
  test.owner.add_field(0x0002, pool.append_utf8("value").value(), pool.append_utf8("I").value()).value();
  const auto value = test.member(CpTag::Fieldref, "value", "I", test.owner.this_class());
  const auto self_init = test.member(CpTag::Methodref, "<init>", "()V", test.owner.this_class());
  const auto super_init = test.member(CpTag::Methodref, "<init>", "()V", test.owner.super_class());
  const auto constructor = &test.method("()V", "<init>", 0x0001);
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, *constructor, &test.hierarchy, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
  const auto run = [&](Instruction instruction, InstructionId at = {}) {
    EXPECT_TRUE(effects.prepare(at, instruction, LOCAL_COUNT));
    return effects.apply(frame, at, instruction);
  };
  constexpr V UNINITIALIZED_THIS{K::UninitializedThis};
  ASSERT_TRUE(frame.constructor_uninitialized);
  EXPECT_EQ(frame.locals.front(), UNINITIALIZED_THIS);
  auto early_return = run(Instruction::return_void());
  ASSERT_FALSE(early_return);
  EXPECT_EQ(early_return.error().kind, AnalysisErrorKind::InvalidConstructorState);
  ASSERT_TRUE(run(Instruction::aload(0)));
  ASSERT_TRUE(run(Instruction::astore(2)));
  ASSERT_TRUE(run(Instruction::aload(2)));
  ASSERT_TRUE(run(simple(Opcode::IConst1)));
  ASSERT_TRUE(run(test.constant(Opcode::PutField, value)));
  ASSERT_TRUE(run(Instruction::aload(2)));
  auto initialized = run(test.constant(Opcode::InvokeSpecial, super_init));
  ASSERT_TRUE(initialized) << initialized.error().detail;
  EXPECT_EQ(*initialized, UNINITIALIZED_THIS);
  EXPECT_FALSE(frame.constructor_uninitialized);
  EXPECT_EQ(frame.locals[0], SELF);
  EXPECT_EQ(frame.locals[2], SELF);
  ASSERT_TRUE(run(Instruction::return_void()));

  const auto allocation = test.constant(Opcode::New, test.owner.this_class());
  constexpr InstructionId FIRST{11, 1};
  constexpr InstructionId SECOND{12, 1};
  ASSERT_TRUE(run(allocation, FIRST));
  ASSERT_TRUE(run(Instruction::astore(1)));
  ASSERT_TRUE(run(allocation, SECOND));
  ASSERT_TRUE(run(Instruction::astore(3)));
  ASSERT_TRUE(run(Instruction::aload(1)));
  ASSERT_TRUE(run(Instruction::dup()));
  initialized = run(test.constant(Opcode::InvokeSpecial, self_init));
  ASSERT_TRUE(initialized);
  EXPECT_EQ(*initialized, (V{K::Uninitialized, SELF.reference, FIRST}));
  EXPECT_EQ(frame.locals[1], SELF);
  EXPECT_EQ(frame.locals[3], (V{K::Uninitialized, SELF.reference, SECOND}));
  ASSERT_EQ(frame.stack.size(), 1u);
  EXPECT_EQ(frame.stack.back(), SELF);
  ASSERT_TRUE(run(allocation, SECOND));
  EXPECT_EQ(frame.locals[3].kind, K::Top);
  const auto repeated = run(allocation, SECOND);
  ASSERT_FALSE(repeated);
  EXPECT_EQ(repeated.error().kind, AnalysisErrorKind::UninitializedUse);

  const auto invalid_constructor = run(test.constant(Opcode::InvokeSpecial, super_init));
  ASSERT_FALSE(invalid_constructor);
  EXPECT_EQ(invalid_constructor.error().kind, AnalysisErrorKind::UninitializedUse);
}

TEST(InstructionEffects, ArrayInstructionsUseExactPrimitiveComponents) {
  TestMethod test;
  const auto& method = test.method();
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, method, &test.hierarchy, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
  const auto run = [&](Opcode opcode, std::initializer_list<V> stack) {
    give_stack(frame, stack);
    EXPECT_TRUE(effects.prepare({}, simple(opcode), LOCAL_COUNT));
    return effects.apply(frame, {}, simple(opcode));
  };
  ASSERT_TRUE(run(Opcode::BALoad, {BOOL_ARRAY, INT}));
  EXPECT_EQ(frame.stack.back(), INT);
  ASSERT_TRUE(run(Opcode::AAStore, {STRING_ARRAY, INT, SELF})); // Runtime component check.
  ASSERT_TRUE(run(Opcode::AALoad, {NULL_VALUE, INT}));
  EXPECT_EQ(frame.stack.back(), NULL_VALUE);
  ASSERT_TRUE(run(Opcode::AALoad, {V{K::Object, "[[D"}, INT}));
  EXPECT_EQ(frame.stack.back(), DOUBLE_ARRAY);
  for (const auto& invalid : {run(Opcode::AALoad, {INT_ARRAY, INT}),
                              run(Opcode::IALoad, {BYTE_ARRAY, INT}),
                              run(Opcode::ArrayLength, {SELF}),
                              run(Opcode::AAStore, {STRING_ARRAY, INT, LONG})}) {
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().kind, AnalysisErrorKind::TypeMismatch);
  }
}

TEST(InstructionEffects, ConstantsAndInvocationValidationUseTagsDescriptorsAndVersions) {
  TestMethod test;
  auto& pool = test.owner.constant_pool();
  const auto double_dynamic = test.member(CpTag::Dynamic, "constant", "D");
  const auto long_dynamic = test.member(CpTag::Dynamic, "constant", "J");
  const auto int_dynamic = test.member(CpTag::Dynamic, "constant", "B");
  const auto object_dynamic = test.member(CpTag::Dynamic, "constant", "[Ljava/lang/String;");
  const auto malformed = test.member(CpTag::Dynamic, "constant", "Jx");
  const auto interface_call = test.member(CpTag::InterfaceMethodref, "call", "(J)V", test.owner.interfaces().front());
  const auto matrix = test.class_index("[[I");
  const auto string = pool.append(CpEntry{CpTag::String, 0, pool.append_utf8("hi").value(), {}}).value();
  const auto method_type = pool.append(CpEntry{CpTag::MethodType, 0, pool.append_utf8("(I)V").value(), {}}).value();
  const auto target = test.member(CpTag::Methodref, "other", "()V", test.owner.super_class());
  const auto handle = pool.append(CpEntry{CpTag::MethodHandle, 6, target, {}}).value();
  const auto& method = test.method();
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, method, &test.hierarchy, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
  for (auto [opcode, constant, expected] : std::array{
           std::tuple{Opcode::Ldc2W, double_dynamic, DOUBLE},
           std::tuple{Opcode::Ldc2W, long_dynamic, LONG},
           std::tuple{Opcode::Ldc, int_dynamic, INT},
           std::tuple{Opcode::Ldc, object_dynamic, STRING_ARRAY},
           std::tuple{Opcode::Ldc, string, STRING},
           std::tuple{Opcode::Ldc, method_type, V{K::Object, "java/lang/invoke/MethodType"}},
           std::tuple{Opcode::Ldc, handle, V{K::Object, "java/lang/invoke/MethodHandle"}},
           std::tuple{Opcode::Ldc, test.owner.this_class(), V{K::Object, "java/lang/Class"}},
       }) {
    const auto instruction = test.constant(opcode, constant);
    ASSERT_TRUE(effects.prepare({}, instruction, LOCAL_COUNT));
    ASSERT_TRUE(effects.apply(frame, {}, instruction));
    ASSERT_FALSE(frame.stack.empty());
    EXPECT_EQ(frame.stack.back(), expected);
  }
  for (const auto instruction : {test.constant(Opcode::New, string),
                                 test.constant(Opcode::Ldc, double_dynamic),
                                 test.constant(Opcode::Ldc2W, malformed),
                                 test.constant(Opcode::GetStatic, target),
                                 Instruction::multi_array(test.owner.this_class(), 1).value()}) {
    const auto invalid = effects.prepare({}, instruction, LOCAL_COUNT);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().kind, AnalysisErrorKind::InvalidConstantPoolReference);
  }
  ASSERT_TRUE(effects.prepare({}, Instruction::invoke_interface(interface_call, 3).value(), LOCAL_COUNT));
  const auto bad_count = effects.prepare({}, Instruction::invoke_interface(interface_call, 2).value(), LOCAL_COUNT);
  ASSERT_FALSE(bad_count);
  EXPECT_EQ(bad_count.error().kind, AnalysisErrorKind::InvalidCode);
  ASSERT_TRUE(effects.prepare({}, Instruction::multi_array(matrix, 1).value(), LOCAL_COUNT));
  const auto bad_dimension = effects.prepare({}, Instruction::multi_array(matrix, 3).value(), LOCAL_COUNT);
  ASSERT_FALSE(bad_dimension);
  EXPECT_EQ(bad_dimension.error().kind, AnalysisErrorKind::InvalidCode);
  ASSERT_FALSE(Instruction::simple(Opcode::Breakpoint));

  TestMethod old(ClassVersion{48, 0});
  const auto& old_method = old.method();
  InstructionEffects old_effects(old.owner, old_method, nullptr, &scratch);
  ASSERT_TRUE(old_effects.initialize(frame, LOCAL_COUNT));
  const auto old_class_constant = old.constant(Opcode::Ldc, old.owner.this_class());
  const auto rejected = old_effects.prepare({}, old_class_constant, LOCAL_COUNT);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, AnalysisErrorKind::InvalidConstantPoolReference);
}

TEST(InstructionEffects, ReceiverMismatchAndMissingHierarchyAreDistinguished) {
  TestMethod test;
  const auto call = test.member(CpTag::Methodref, "other", "()V", test.owner.super_class());
  const auto invocation = test.constant(Opcode::InvokeVirtual, call);
  const auto& method = test.method();
  std::pmr::unsynchronized_pool_resource scratch;
  InstructionEffects effects(test.owner, method, &test.hierarchy, &scratch);
  Frame frame(&scratch);
  ASSERT_TRUE(effects.initialize(frame, LOCAL_COUNT));
  ASSERT_TRUE(effects.prepare({}, invocation, LOCAL_COUNT));
  const std::array string_receiver{STRING};
  give_stack(frame, string_receiver);
  const auto mismatch = effects.apply(frame, {}, invocation);
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().kind, AnalysisErrorKind::TypeMismatch);

  constexpr V UNKNOWN{K::Object, "missing/Type"};
  const std::array missing_receiver{UNKNOWN};
  give_stack(frame, missing_receiver);
  const auto unresolved = effects.apply(frame, {}, invocation);
  ASSERT_FALSE(unresolved);
  EXPECT_EQ(unresolved.error().kind, AnalysisErrorKind::TypeResolutionFailure);
  ASSERT_TRUE(unresolved.error().resolution);
  EXPECT_EQ(unresolved.error().resolution->name, UNKNOWN.reference);

  InstructionEffects widths(test.owner, method, nullptr, &scratch);
  ASSERT_TRUE(widths.initialize(frame, LOCAL_COUNT));
  ASSERT_TRUE(widths.prepare({}, invocation, LOCAL_COUNT));
  give_stack(frame, missing_receiver);
  ASSERT_TRUE(widths.apply(frame, {}, invocation));
}

} // namespace
} // namespace cafe::analysis
