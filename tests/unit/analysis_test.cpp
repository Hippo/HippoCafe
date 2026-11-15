#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cafe/analysis/control_flow_graph.hpp"
#include "cafe/analysis/frame_analyzer.hpp"
#include "cafe/analysis/max_locals.hpp"
#include "cafe/analysis/max_stack.hpp"
#include "cafe/class_file.hpp"
#include "cafe/classpath/class_path.hpp"

namespace cafe::analysis {
namespace {

constexpr std::uint16_t STATIC_PUBLIC = 0x0009;
constexpr ClassVersion MODERN{69, 0};

CpIndex add_class(ConstantPool& pool, std::string_view name) {
  const auto utf8 = pool.append_utf8(name).value();
  return pool.append(CpEntry{CpTag::Class, 0, utf8, {}}).value();
}

std::size_t add_method(ClassFile& owner, std::string_view signature,
                       std::uint16_t flags = STATIC_PUBLIC, std::string_view name = "test") {
  auto& pool = owner.constant_pool();
  const auto descriptor = pool.append_utf8(signature).value();
  const auto name_index = pool.append_utf8(name).value();
  const auto index = owner.add_method(flags, name_index, descriptor).value();
  const auto code_name = pool.append_utf8("Code").value();
  std::ignore = owner.add_code(index, code_name).value();
  return index;
}

Instruction simple(Opcode opcode) {
  return Instruction::simple(opcode).value();
}

LabelId label_at(MethodCode& code, std::optional<InstructionId> id) {
  const auto label = code.make_label().value();
  code.bind_label(label, {id}).value();
  return label;
}

TEST(ControlFlowGraph, RetainsSwitchKeysAndBackEdgesWithStableInstructionHandles) {
  MethodCode code;
  const auto entry = code.append(simple(Opcode::IConst0)).value();
  const auto begin = label_at(code, entry);
  const auto fallback = code.make_label().value();
  const std::array targets{begin, begin};
  const auto switch_id = code.append(TableSwitch{-3, fallback, targets}).value();
  const auto return_id = code.append(Instruction::return_void()).value();
  code.bind_label(fallback, {return_id}).value();

  auto cfg = build_cfg(code);
  ASSERT_TRUE(cfg) << cfg.error().detail;
  ASSERT_EQ(cfg->blocks().size(), 2);
  EXPECT_TRUE(cfg->blocks().front().frame_boundary);
  EXPECT_TRUE(cfg->blocks()[1].reachable);
  EXPECT_EQ(cfg->block_of(switch_id), BlockId{0});
  EXPECT_EQ(cfg->block_of(return_id), BlockId{1});
  const auto edges = cfg->outgoing(BlockId{0});
  ASSERT_EQ(edges.size(), 3);
  EXPECT_EQ(edges[0].kind, EdgeKind::SwitchDefault);
  EXPECT_EQ(edges[0].target, BlockId{1});
  EXPECT_EQ(edges[1].kind, EdgeKind::SwitchCase);
  EXPECT_EQ(edges[1].case_key, -3);
  EXPECT_EQ(edges[1].target, BlockId{0});
  EXPECT_EQ(edges[2].case_key, -2);
  EXPECT_EQ(edges[2].target, BlockId{0});
  EXPECT_TRUE(cfg->is_current(code, {}));
  code.replace(entry, simple(Opcode::IConst1)).value();
  EXPECT_FALSE(cfg->is_current(code, {}));
}

TEST(ControlFlowGraph, IndexesOverlappingRangesAndDetectsExceptionTableChanges) {
  MethodCode code;
  auto ids = code.append(std::array<InstructionBuilder, 6>{
                             Instruction::nop(), Instruction::nop(), Instruction::nop(), Instruction::return_void(),
                             Instruction::pop(), Instruction::return_void()})
                 .value();
  const auto first = label_at(code, ids[0]);
  const auto second = label_at(code, ids[1]);
  const auto fourth = label_at(code, ids[3]);
  const auto target = label_at(code, ids[4]);
  const auto end = label_at(code, std::nullopt);
  std::array handlers{
      ExceptionHandler{second, end, target, {}},
      ExceptionHandler{first, fourth, target, {}},
  };
  auto cfg = build_cfg(code, handlers);
  ASSERT_TRUE(cfg) << cfg.error().detail;
  ASSERT_EQ(cfg->blocks().size(), 4);
  std::pmr::vector<std::uint32_t> ranges;
  cfg->handlers_for(cfg->block_of(ids[0]).value(), ranges);
  EXPECT_EQ(ranges, (std::pmr::vector<std::uint32_t>{1}));
  cfg->handlers_for(cfg->block_of(ids[2]).value(), ranges);
  EXPECT_EQ(ranges, (std::pmr::vector<std::uint32_t>{0, 1}));
  cfg->handlers_for(cfg->block_of(ids[4]).value(), ranges);
  EXPECT_EQ(ranges, (std::pmr::vector<std::uint32_t>{0}));
  EXPECT_EQ(cfg->exception_ranges()[0].end, code.size());
  EXPECT_TRUE(cfg->is_current(code, handlers));
  handlers[0].catch_type = CpIndex{4};
  EXPECT_FALSE(cfg->is_current(code, handlers));

  handlers[0].end = second;
  auto invalid = build_cfg(code, handlers);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().kind, AnalysisErrorKind::InvalidLabelOrRange);
  EXPECT_EQ(invalid.error().label, second);
}

TEST(ControlFlowGraph, ValidatesTargetsAndReachableFallthrough) {
  MethodCode code;
  const auto target = code.make_label().value();
  const auto branch = code.append(Instruction::branch(Opcode::Goto, target).value()).value();
  auto cfg = build_cfg(code);
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().kind, AnalysisErrorKind::InvalidLabelOrRange);
  EXPECT_EQ(cfg.error().instruction, branch);
  EXPECT_EQ(cfg.error().label, target);

  code.bind_label(target, {std::nullopt}).value();
  cfg = build_cfg(code);
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().kind, AnalysisErrorKind::InvalidLabelOrRange);

  code.replace(branch, Instruction::nop()).value();
  cfg = build_cfg(code);
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().kind, AnalysisErrorKind::InvalidCode);
  EXPECT_EQ(cfg.error().instruction, branch);
}

TEST(AnalysisMaxima, AccountsForParameterWidthsAndWideLocalLimits) {
  auto owner = ClassFile::create(MODERN, "Locals").value();
  const auto method = add_method(owner, "(JD[I)V", 0x0001);
  auto& code = owner.edit_code(method)->code();
  const auto slot = code.append(Instruction::increment(400, -12345)).value();
  code.append(Instruction::return_void()).value();
  EXPECT_EQ(compute_max_locals(owner.methods()[method], owner).value(), 401);
  code.replace(slot, Instruction::nop()).value();
  EXPECT_EQ(compute_max_locals(owner.methods()[method], owner).value(), 6);
  code.replace(slot, Instruction::lstore(65533)).value();
  EXPECT_EQ(compute_max_locals(owner.methods()[method], owner).value(), 65535);
  code.replace(slot, Instruction::lload(65534)).value();
  auto exceeded = compute_max_locals(owner.methods()[method], owner);
  ASSERT_FALSE(exceeded);
  EXPECT_EQ(exceeded.error().kind, AnalysisErrorKind::LocalOverflow);
  EXPECT_EQ(exceeded.error().instruction, slot);
  EXPECT_EQ(exceeded.error().class_name, "Locals");
  EXPECT_EQ(exceeded.error().method_name, "test");
  EXPECT_EQ(exceeded.error().descriptor, "(JD[I)V");
}

TEST(AnalysisMaxima, RejectsEqualSlotJoinsWithDifferentCategoryShapes) {
  auto owner = ClassFile::create(MODERN, "Widths").value();
  const auto method = add_method(owner, "(I)V");
  auto& code = owner.edit_code(method)->code();
  const auto alternate = code.make_label().value();
  const auto join = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 8>{
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, alternate).value(),
                             simple(Opcode::LConst0), Instruction::branch(Opcode::Goto, join).value(),
                             simple(Opcode::IConst0), simple(Opcode::IConst1), Instruction::pop2(),
                             Instruction::return_void()})
                 .value();
  code.bind_label(alternate, {ids[4]}).value();
  code.bind_label(join, {ids[6]}).value();
  auto max_stack = compute_max_stack(owner, owner.methods()[method]);
  ASSERT_FALSE(max_stack);
  EXPECT_EQ(max_stack.error().kind, AnalysisErrorKind::IncompatibleJoin);
  EXPECT_EQ(max_stack.error().instruction, ids[6]);
}

TEST(FrameAnalysis, HoldsOwnedReferenceNamesAfterModelAndContextDestruction) {
  const auto analyze = [] {
    auto owner = ClassFile::create(MODERN, "Owned").value();
    const auto method = add_method(owner, "(I[Lsome/LongName;)[Lsome/LongName;");
    auto& code = owner.edit_code(method)->code();
    const auto alternate = code.make_label().value();
    const auto join = code.make_label().value();
    auto instructions = code.append(std::array<InstructionBuilder, 6>{
                                        Instruction::iload(0), Instruction::branch(Opcode::IfEq, alternate).value(),
                                        Instruction::aload(1), Instruction::branch(Opcode::Goto, join).value(),
                                        simple(Opcode::AConstNull), simple(Opcode::AReturn)})
                            .value();
    code.bind_label(alternate, {instructions[4]}).value();
    code.bind_label(join, {instructions[5]}).value();
    AnalysisContext context;
    return compute_frames(owner, owner.methods()[method], context);
  };
  auto result = analyze();
  ASSERT_TRUE(result) << result.error().detail;
  EXPECT_EQ(result->max_stack(), 1);
  EXPECT_EQ(result->max_locals(), 2);
  ASSERT_EQ(result->frames().size(), 2);
  const auto frames = result->frames();
  const auto& types = frames.back().frame;
  ASSERT_EQ(types.stack.size(), 1);
  EXPECT_EQ(types.stack[0].kind, VerificationKind::Object);
  EXPECT_EQ(types.stack[0].reference, "[Lsome/LongName;");
  EXPECT_EQ(types.locals[1].reference, "[Lsome/LongName;");
}

TEST(FrameAnalysis, DetectsOwnerAndResolutionSnapshotChanges) {
  auto owner = ClassFile::create(MODERN, "Stamps").value();
  const auto method = add_method(owner, "()V");
  owner.edit_code(method)->code().append(Instruction::return_void()).value();
  AnalysisContext context;
  auto computed = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(computed) << computed.error().detail;
  EXPECT_TRUE(computed->is_current(owner, owner.methods()[method], context));

  auto same_snapshot = context;
  EXPECT_TRUE(computed->is_current(owner, owner.methods()[method], same_snapshot));
  AnalysisContext other_snapshot;
  EXPECT_FALSE(computed->is_current(owner, owner.methods()[method], other_snapshot));

  owner.add_interface(add_class(owner.constant_pool(), "api/Added")).value();
  EXPECT_FALSE(computed->is_current(owner, owner.methods()[method], context));
  EXPECT_TRUE(computed->is_current(owner.methods()[method].code()->code(), {}));
}

TEST(FrameAnalysis, ResolvesSharedInterfaceAcrossAnInMemoryDiamond) {
  auto root = ClassFile::create(MODERN, "java/lang/Object", std::nullopt).value();
  auto api = ClassFile::create(MODERN, "api/Shared", "java/lang/Object", 0x0601).value();
  auto left = ClassFile::create(MODERN, "pkg/Left").value();
  auto right = ClassFile::create(MODERN, "pkg/Right").value();
  left.add_interface(add_class(left.constant_pool(), "api/Shared")).value();
  right.add_interface(add_class(right.constant_pool(), "api/Shared")).value();

  auto owner = ClassFile::create(MODERN, "Diamond").value();
  const auto method = add_method(owner, "(ILpkg/Left;Lpkg/Right;)Lapi/Shared;");
  auto& code = owner.edit_code(method)->code();
  const auto alternate = code.make_label().value();
  const auto join = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 6>{
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, alternate).value(),
                             Instruction::aload(1), Instruction::branch(Opcode::Goto, join).value(),
                             Instruction::aload(2), simple(Opcode::AReturn)})
                 .value();
  code.bind_label(alternate, {ids[4]}).value();
  code.bind_label(join, {ids[5]}).value();
  classpath::ClassPathBuilder builder;
  for (const auto* source : {&root, &api, &left, &right})
    builder.add_class(*source).value();
  AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  ASSERT_EQ(frames->frames().size(), 2);
  const auto entries = frames->frames();
  const auto& join_frame = entries.back();
  EXPECT_EQ(join_frame.instruction, ids[5]);
  ASSERT_EQ(join_frame.frame.stack.size(), 1);
  EXPECT_EQ(join_frame.frame.stack[0].reference, "api/Shared");
  EXPECT_EQ(frames->max_stack(), compute_max_stack(owner, owner.methods()[method]).value());

  AnalysisContext incomplete;
  auto missing = compute_frames(owner, owner.methods()[method], incomplete);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().kind, AnalysisErrorKind::TypeResolutionFailure);
  ASSERT_TRUE(missing.error().resolution);
  EXPECT_EQ(missing.error().resolution->kind, classpath::ResolutionErrorKind::NotFound);
}

TEST(FrameAnalysis, RetriesCommonInterfacesAndVerifiesTheirDownstreamUses) {
  auto root = ClassFile::create(MODERN, "java/lang/Object", std::nullopt).value();
  auto first = ClassFile::create(MODERN, "api/First", "java/lang/Object", 0x0601).value();
  auto second = ClassFile::create(MODERN, "api/Second", "java/lang/Object", 0x0601).value();
  auto left = ClassFile::create(MODERN, "pkg/Left").value();
  auto right = ClassFile::create(MODERN, "pkg/Right").value();
  for (auto* interface : {&first, &second}) {
    auto& constants = interface->constant_pool();
    const auto name = constants.append_utf8("use").value();
    const auto signature = constants.append_utf8("()V").value();
    interface->add_method(0x0401, name, signature).value();
  }
  for (auto* implementation : {&left, &right}) {
    implementation->add_interface(add_class(implementation->constant_pool(), "api/First")).value();
    implementation->add_interface(add_class(implementation->constant_pool(), "api/Second")).value();
  }

  auto owner = ClassFile::create(MODERN, "Ambiguous").value();
  auto& pool = owner.constant_pool();
  const auto name = pool.append_utf8("use").value();
  const auto descriptor = pool.append_utf8("()V").value();
  const auto name_type = pool.append(CpEntry{CpTag::NameAndType, 0, name, descriptor}).value();
  const auto invoke = [&](std::string_view name) {
    const auto type = add_class(pool, name);
    const auto ref = pool.append(CpEntry{CpTag::InterfaceMethodref, 0, type, name_type}).value();
    return Instruction::invoke_interface(ref, 1).value();
  };
  const auto method = add_method(owner, "(ILpkg/Left;Lpkg/Right;)V");
  auto& code = owner.edit_code(method)->code();
  const auto alternate = code.make_label().value();
  const auto join = code.make_label().value();
  const auto ids = code.append(std::array<InstructionBuilder, 9>{
                                   Instruction::iload(0), Instruction::branch(Opcode::IfEq, alternate).value(),
                                   Instruction::aload(1), Instruction::branch(Opcode::Goto, join).value(), Instruction::aload(2),
                                   Instruction::dup(), invoke("api/Second"), invoke("api/First"), Instruction::return_void()})
                       .value();
  code.bind_label(alternate, {ids[4]}).value();
  code.bind_label(join, {ids[5]}).value();
  classpath::ClassPathBuilder builder;
  for (const auto* source : {&root, &first, &second, &left, &right})
    builder.add_class(*source).value();
  AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_FALSE(frames);
  EXPECT_EQ(frames.error().kind, AnalysisErrorKind::AmbiguousTypeJoin) << frames.error().detail;

  // The first ordered candidate still cannot satisfy this use.
  code.replace(ids[7], Instruction::pop()).value();
  frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  const auto entries = frames->frames();
  ASSERT_EQ(entries.back().instruction, ids[5]);
  ASSERT_EQ(entries.back().frame.stack.size(), 1);
  EXPECT_EQ(entries.back().frame.stack.front().reference, "api/Second");

  // Both joins see the same incoming references but need different
  // candidates. The first joins a local and the second joins the stack.
  const auto mixed_method = add_method(owner, "(IILpkg/Left;Lpkg/Right;)V", STATIC_PUBLIC, "mixed");
  auto& mixed = owner.edit_code(mixed_method)->code();
  const auto local_alternate = mixed.make_label().value();
  const auto local_join = mixed.make_label().value();
  const auto stack_alternate = mixed.make_label().value();
  const auto stack_join = mixed.make_label().value();
  const auto mixed_ids = mixed.append(std::array<InstructionBuilder, 16>{
                                          Instruction::iload(0), Instruction::branch(Opcode::IfEq, local_alternate).value(),
                                          Instruction::aload(2), Instruction::astore(4), Instruction::branch(Opcode::Goto, local_join).value(),
                                          Instruction::aload(3), Instruction::astore(4), Instruction::aload(4), invoke("api/First"),
                                          Instruction::iload(1), Instruction::branch(Opcode::IfEq, stack_alternate).value(),
                                          Instruction::aload(2), Instruction::branch(Opcode::Goto, stack_join).value(),
                                          Instruction::aload(3), invoke("api/Second"), Instruction::return_void()})
                             .value();
  mixed.bind_label(local_alternate, {mixed_ids[5]}).value();
  mixed.bind_label(local_join, {mixed_ids[7]}).value();
  mixed.bind_label(stack_alternate, {mixed_ids[13]}).value();
  mixed.bind_label(stack_join, {mixed_ids[14]}).value();

  const auto independently_selected = compute_frames(owner, owner.methods()[mixed_method], context);
  ASSERT_TRUE(independently_selected) << independently_selected.error().detail;
  const auto mixed_frames = independently_selected->frames();
  const auto local_frame = std::ranges::find(mixed_frames, mixed_ids[7], &StackMapFrame::instruction);
  ASSERT_NE(local_frame, mixed_frames.end());
  ASSERT_EQ(local_frame->frame.locals.size(), 5);
  EXPECT_EQ(local_frame->frame.locals[4].reference, "api/First");
  const auto stack_frame = std::ranges::find(mixed_frames, mixed_ids[14], &StackMapFrame::instruction);
  ASSERT_NE(stack_frame, mixed_frames.end());
  ASSERT_EQ(stack_frame->frame.stack.size(), 1);
  EXPECT_EQ(stack_frame->frame.stack.front().reference, "api/Second");
}

TEST(FrameAnalysis, ClearsUnusableWideLocalsAcrossJoins) {
  auto owner = ClassFile::create(MODERN, "WideJoin").value();
  const auto method = add_method(owner, "(I)V");
  auto& code = owner.edit_code(method)->code();
  const auto alternate = code.make_label().value();
  const auto join = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 10>{
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, alternate).value(),
                             simple(Opcode::LConst0), Instruction::lstore(1), Instruction::branch(Opcode::Goto, join).value(),
                             simple(Opcode::FConst0), Instruction::fstore(1), simple(Opcode::IConst0), Instruction::istore(2),
                             Instruction::return_void()})
                 .value();
  code.bind_label(alternate, {ids[5]}).value();
  code.bind_label(join, {ids[9]}).value();
  AnalysisContext context;
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  EXPECT_EQ(frames->max_stack(), 2);
  EXPECT_EQ(frames->max_locals(), 3);
  EXPECT_EQ(frames->frames().back().instruction, ids[9]);
  ASSERT_EQ(frames->frames().back().frame.locals.size(), 1);
  EXPECT_EQ(frames->frames().back().frame.locals.front().kind, VerificationKind::Integer);
}

TEST(FrameAnalysis, GivesHandlersTheIncomingLocalStateEvenForAStore) {
  auto owner = ClassFile::create(MODERN, "Incoming").value();
  const auto method = add_method(owner, "(I)V");
  auto* attribute = owner.edit_code(method);
  auto& code = attribute->code();
  auto ids = code.append(std::array<InstructionBuilder, 7>{
                             simple(Opcode::AConstNull), Instruction::astore(0), Instruction::return_void(), Instruction::pop(),
                             Instruction::iload(0), Instruction::pop(), Instruction::return_void()})
                 .value();
  const std::array handlers{ExceptionHandler{label_at(code, ids[1]), label_at(code, ids[2]), label_at(code, ids[3]), {}}};
  attribute->set_exception_handlers(handlers);

  AnalysisContext context;
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  EXPECT_TRUE(frames->is_current(code, attribute->exception_handlers()));
  EXPECT_EQ(frames->max_stack(), 1);
  EXPECT_EQ(frames->max_locals(), 1);
  ASSERT_EQ(frames->frames().size(), 1);
  const auto entries = frames->frames();
  const auto& handler = entries.front();
  EXPECT_EQ(handler.instruction, ids[3]);
  ASSERT_EQ(handler.frame.stack.size(), 1);
  EXPECT_EQ(handler.frame.stack.front().reference, "java/lang/Throwable");
  ASSERT_EQ(handler.frame.locals.size(), 1);
  EXPECT_EQ(handler.frame.locals.front().kind, VerificationKind::Integer);
  attribute->set_exception_handlers({});
  EXPECT_FALSE(frames->is_current(code, attribute->exception_handlers()));
}

CpIndex object_constructor(const ClassFile& owner, ConstantPool& pool) {
  const auto name = pool.append_utf8("<init>").value();
  const auto descriptor = pool.append_utf8("()V").value();
  const auto signature = pool.append(CpEntry{CpTag::NameAndType, 0, name, descriptor}).value();
  return pool.append(CpEntry{CpTag::Methodref, 0, owner.super_class(), signature}).value();
}

AnalysisContext object_context() {
  auto root = ClassFile::create(MODERN, "java/lang/Object", std::nullopt).value();
  const auto constructor = add_method(root, "()V", 0x0001, "<init>");
  root.edit_code(constructor)->code().append(Instruction::return_void()).value();
  classpath::ClassPathBuilder builder;
  builder.add_class(root).value();
  return AnalysisContext{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
}

TEST(FrameAnalysis, MakesStoredAllocationAliasesUnusableOnConstructorExceptions) {
  auto owner = ClassFile::create(MODERN, "Allocation").value();
  auto& pool = owner.constant_pool();
  const auto object = owner.super_class();
  const auto constructor = object_constructor(owner, pool);
  const auto method = add_method(owner, "()V");
  auto* attribute = owner.edit_code(method);
  auto& code = attribute->code();
  auto ids = code.append(std::array<InstructionBuilder, 7>{
                             Instruction::constant(Opcode::New, object).value(), Instruction::dup(), Instruction::astore(0),
                             Instruction::constant(Opcode::InvokeSpecial, constructor).value(), Instruction::return_void(),
                             Instruction::pop(), Instruction::return_void()})
                 .value();
  const std::array handlers{ExceptionHandler{label_at(code, ids[3]), label_at(code, ids[4]), label_at(code, ids[5]), {}}};
  attribute->set_exception_handlers(handlers);
  auto context = object_context();
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  EXPECT_EQ(frames->max_stack(), 2);
  EXPECT_EQ(frames->max_stack(), compute_max_stack(owner, owner.methods()[method]).value());
  EXPECT_EQ(frames->max_locals(), 1);
  ASSERT_EQ(frames->frames().size(), 1);
  EXPECT_EQ(frames->frames().front().instruction, ids[5]);
  EXPECT_TRUE(frames->frames().front().frame.locals.empty());

  code.replace(ids[6], Instruction::aload(0)).value();
  code.append(Instruction::pop()).value();
  code.append(Instruction::return_void()).value();
  auto bad_alias = compute_frames(owner, owner.methods()[method], context);
  ASSERT_FALSE(bad_alias);
  EXPECT_EQ(bad_alias.error().kind, AnalysisErrorKind::TypeMismatch);
  EXPECT_EQ(bad_alias.error().instruction, ids[6]);
}

TEST(FrameAnalysis, ResolvesCatchTypesAgainstTheCurrentOwnerDefinition) {
  auto root = ClassFile::create(MODERN, "java/lang/Object", std::nullopt).value();
  auto throwable = ClassFile::create(MODERN, "java/lang/Throwable").value();
  auto shadowed = ClassFile::create(MODERN, "HandlerType").value();
  auto owner = ClassFile::create(MODERN, "HandlerType", "java/lang/Throwable").value();
  const auto method = add_method(owner, "()V");
  auto* attribute = owner.edit_code(method);
  auto& code = attribute->code();
  const auto ids = code.append(std::array<InstructionBuilder, 4>{
                                   Instruction::nop(), Instruction::return_void(), Instruction::pop(), Instruction::return_void()})
                       .value();
  const std::array handlers{ExceptionHandler{label_at(code, ids[0]), label_at(code, ids[1]),
                                             label_at(code, ids[2]), owner.this_class()}};
  attribute->set_exception_handlers(handlers);

  classpath::ClassPathBuilder builder;
  for (const auto* definition : {&root, &throwable, &shadowed})
    builder.add_class(*definition).value();
  AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  const auto entries = frames->frames();
  ASSERT_EQ(entries.size(), 1);
  ASSERT_EQ(entries.front().frame.stack.size(), 1);
  EXPECT_EQ(entries.front().frame.stack.front().reference, "HandlerType");
}

TEST(FrameAnalysis, RejectsHandlerOverSuperCallWhenConstructorFlagCannotBeEncoded) {
  auto owner = ClassFile::create(MODERN, "Constructor").value();
  const auto constructor = object_constructor(owner, owner.constant_pool());
  const auto method = add_method(owner, "()V", 0x0001, "<init>");
  auto* attribute = owner.edit_code(method);
  auto& code = attribute->code();
  auto ids = code.append(std::array<InstructionBuilder, 4>{
                             Instruction::aload(0), Instruction::constant(Opcode::InvokeSpecial, constructor).value(),
                             Instruction::return_void(), simple(Opcode::AThrow)})
                 .value();
  const std::array handlers{ExceptionHandler{label_at(code, ids[1]), label_at(code, ids[2]), label_at(code, ids[3]), {}}};
  attribute->set_exception_handlers(handlers);
  auto context = object_context();
  auto bad_handler = compute_frames(owner, owner.methods()[method], context);
  ASSERT_FALSE(bad_handler);
  EXPECT_EQ(bad_handler.error().kind, AnalysisErrorKind::UnrepresentableFrame);
  EXPECT_EQ(bad_handler.error().instruction, ids[3]);
}

TEST(FrameAnalysis, CarriesAllocationIdentityAndFindsTheRequiredFrameAtEntry) {
  auto owner = ClassFile::create(MODERN, "AllocationFrames").value();
  const auto constructor = object_constructor(owner, owner.constant_pool());
  const auto method = add_method(owner, "(I)V");
  auto& code = owner.edit_code(method)->code();
  const auto entry = code.make_label().value();
  const auto initialize = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 9>{
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, entry).value(),
                             Instruction::constant(Opcode::New, owner.super_class()).value(), Instruction::iload(0),
                             Instruction::branch(Opcode::IfEq, initialize).value(), Instruction::nop(),
                             Instruction::constant(Opcode::InvokeSpecial, constructor).value(),
                             Instruction::nop(), Instruction::return_void()})
                 .value();
  code.bind_label(entry, {ids[0]}).value();
  code.bind_label(initialize, {ids[6]}).value();
  auto context = object_context();
  auto frames = compute_frames(owner, owner.methods()[method], context);
  ASSERT_TRUE(frames) << frames.error().detail;
  ASSERT_EQ(frames->frames().size(), 2);
  EXPECT_EQ(frames->frames().front().instruction, ids[0]);
  EXPECT_EQ(frames->frames().back().instruction, ids[6]);
  const auto entries = frames->frames();
  const auto& stack = entries.back().frame.stack;
  ASSERT_EQ(stack.size(), 1);
  EXPECT_EQ(stack.front().kind, VerificationKind::Uninitialized);
  EXPECT_EQ(stack.front().allocation, ids[2]);
  code.insert_before(ids[2], Instruction::nop()).value();
  EXPECT_FALSE(frames->is_current(code, {}));
}

TEST(FrameAnalysis, RejectsDeadModernCodeAndKeepsLegacySubroutinesDistinct) {
  AnalysisContext context;
  auto modern = ClassFile::create(MODERN, "Unreachable").value();
  const auto current = add_method(modern, "()V");
  auto& code = modern.edit_code(current)->code();
  code.append(Instruction::return_void()).value();
  const auto dead = code.append(Instruction::return_void()).value();
  auto unsupported = compute_frames(modern, modern.methods()[current], context);
  ASSERT_FALSE(unsupported);
  EXPECT_EQ(unsupported.error().kind, AnalysisErrorKind::UnreachableCode);
  EXPECT_EQ(unsupported.error().instruction, dead);
  EXPECT_EQ(compute_max_stack(modern, modern.methods()[current]).value(), 0);

  auto legacy = ClassFile::create(ClassVersion{49, 0}, "Legacy").value();
  const auto historical = add_method(legacy, "()V");
  auto& legacy_code = legacy.edit_code(historical)->code();
  legacy_code.append(Instruction::return_void()).value();
  legacy_code.append(Instruction::return_void()).value();
  auto inferred = compute_frames(legacy, legacy.methods()[historical], context);
  ASSERT_TRUE(inferred) << inferred.error().detail;
  EXPECT_TRUE(inferred->frames().empty());

  const auto subroutine = label_at(legacy_code, legacy_code.at(1));
  legacy_code.replace(legacy_code.at(0), Instruction::branch(Opcode::Jsr, subroutine).value()).value();
  legacy_code.replace(legacy_code.at(1), Instruction::local(Opcode::Ret, 0).value()).value();
  auto max_stack = compute_max_stack(legacy, legacy.methods()[historical]);
  ASSERT_FALSE(max_stack);
  EXPECT_EQ(max_stack.error().kind, AnalysisErrorKind::UnsupportedSubroutineAnalysis);
}

TEST(AnalysisMaxima, AppliesTheShortHistoricCodeHeaderAndRejectsUnknownVerifierVersions) {
  auto early = ClassFile::create(ClassVersion{45, 2}, "ShortCode").value();
  const auto method = add_method(early, "()V");
  auto& code = early.edit_code(method)->code();
  std::vector<InstructionBuilder> push(128, simple(Opcode::LConst0));
  code.append(push).value();
  code.append(Instruction::return_void()).value();
  auto stack = compute_max_stack(early, early.methods()[method]);
  ASSERT_FALSE(stack);
  EXPECT_EQ(stack.error().kind, AnalysisErrorKind::StackOverflow);

  code.insert_before(code.at(0), Instruction::istore(255)).value();
  auto locals = compute_max_locals(early.methods()[method], early);
  ASSERT_FALSE(locals);
  EXPECT_EQ(locals.error().kind, AnalysisErrorKind::LocalOverflow);

  auto future = ClassFile::create(ClassVersion{70, 0}, "Future").value();
  const auto unknown = add_method(future, "()V");
  future.edit_code(unknown)->code().append(Instruction::return_void()).value();
  auto max_stack = compute_max_stack(future, future.methods()[unknown]);
  ASSERT_FALSE(max_stack);
  EXPECT_EQ(max_stack.error().kind, AnalysisErrorKind::UnsupportedClassVersion);
}

TEST(AnalysisLimits, EnforcesEdgeWorkMemoryFrameAndCancellationBudgets) {
  auto owner = ClassFile::create(MODERN, "Bounded").value();
  const auto method = add_method(owner, "(I)V");
  auto& code = owner.edit_code(method)->code();
  const auto target = code.make_label().value();
  auto ids = code.append(std::array<InstructionBuilder, 3>{
                             Instruction::iload(0), Instruction::branch(Opcode::IfEq, target).value(),
                             Instruction::return_void()})
                 .value();
  code.bind_label(target, {ids[2]}).value();

  for (const auto& limits : {AnalysisLimits{.max_edges = 1}, AnalysisLimits{.max_work = 5},
                             AnalysisLimits{.max_memory_bytes = 1}, AnalysisLimits{.max_stack_slots = 0},
                             AnalysisLimits{.max_frames = 0}}) {
    AnalysisContext context(limits);
    auto result = compute_frames(owner, owner.methods()[method], context);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, AnalysisErrorKind::ResourceLimitExceeded) << result.error().detail;
  }
  std::stop_source cancelled;
  cancelled.request_stop();
  auto result = compute_max_stack(owner, owner.methods()[method], {.cancellation = cancelled.get_token()});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().kind, AnalysisErrorKind::Cancelled);
}

} // namespace
} // namespace cafe::analysis
