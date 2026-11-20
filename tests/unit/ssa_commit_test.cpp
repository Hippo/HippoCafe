#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

#include "analysis/ssa_commit.hpp"
#include "analysis/ssa_frames.hpp"
#include "io/ssa_relocation.hpp"

namespace cafe::detail {
namespace {

std::size_t declare(ClassFile& owner, std::string_view name = "run", std::string_view descriptor = "()V") {
  auto& pool = owner.constant_pool();
  return owner.add_method(0x0009, pool.append_utf8(name).value(), pool.append_utf8(descriptor).value()).value();
}

TEST(SsaCommit, AttachesNewCodeForDefaultWriterAndInvalidatesStampAfterRawEdit) {
  auto owner = ClassFile::create({61, 0}, "NewBody").value();
  const auto index = declare(owner);
  analysis::AnalysisContext context;
  const auto stamp = SsaCommit::source_stamp(owner, index, context);
  SsaCommit tx(owner, index);
  const auto& original_address = tx.code();
  ASSERT_TRUE(tx.code().append(Instruction::return_void()));
  const auto published = tx.finish(stamp, context, {}, {}, SsaCodeProvenance{}, false);
  ASSERT_TRUE(published) << published.error().detail;
  auto* method = owner.edit_code(index);
  ASSERT_NE(method, nullptr);
  EXPECT_NE(&method->code(), &original_address);
  EXPECT_NE(SsaCommit::source_stamp(owner, index, context), stamp);
  const auto result = write_class(owner);
  ASSERT_TRUE(result) << result.error().detail;
  const auto forced = write_class(owner, {.force_reencode = true});
  ASSERT_TRUE(forced) << forced.error().detail;
  EXPECT_EQ(forced->bytes, result->bytes);
  ASSERT_TRUE(read_class(result->bytes, {.mode = ParseMode::Strict}));
  ASSERT_TRUE(method->code().insert_before(method->code().at(0), Instruction::nop()));
  const auto stale = write_class(owner);
  ASSERT_FALSE(stale);
  EXPECT_EQ(stale.error().kind, WriteErrorKind::StaleCodeMetadata);
}

TEST(SsaCommit, PublicPipelineWritesAndReliftsEverySupportedClassMajor) {
  for (std::uint16_t major = 45; major <= 69; ++major) {
    SCOPED_TRACE(major);
    auto owner = ClassFile::create({major, 0}, "VersionedBody").value();
    const auto index = declare(owner, "value", "(J)J");
    analysis::AnalysisContext context;
    auto graph = analysis::create_ssa(owner, index, context).value();
    (void)graph.append_terminator(graph.entry_block(), Opcode::LReturn, graph.parameters()).value();
    const auto lowered = analysis::lower_ssa(owner, graph, context);
    ASSERT_TRUE(lowered) << lowered.error().detail;
    const auto bytes = write_class(owner);
    ASSERT_TRUE(bytes) << bytes.error().detail;
    auto parsed = read_class(bytes->bytes, {.mode = ParseMode::Strict});
    ASSERT_TRUE(parsed) << parsed.error().detail;
    EXPECT_EQ(parsed->class_file.version().major, major);
    auto lifted = analysis::lift_ssa(parsed->class_file, index, context);
    ASSERT_TRUE(lifted) << lifted.error().detail;
    EXPECT_TRUE(analysis::verify_ssa(parsed->class_file, *lifted, context));
  }
}

TEST(SsaCommit, RetryWithOriginalGraphAfterWorkMemoryCancellationAndForeignHierarchyFailures) {
  auto owner = ClassFile::create({61, 0}, "RetryBody").value();
  const auto index = declare(owner);
  auto& body = owner.add_code(index, owner.constant_pool().append_utf8("Code").value()).value().get();
  const auto ret = body.code().append(Instruction::return_void()).value();
  body.supply_metadata(0, 0, {});
  analysis::AnalysisContext context;
  auto graph = analysis::lift_ssa(owner, index, context).value();
  const auto op = graph.block(graph.entry_block())->operations.back();
  const auto original_stamp = SsaCommit::source_stamp(owner, index, context);
  const auto pool_size = owner.constant_pool().size();
  const WriteOptions supplied{.code_policy = CodeWritePolicy::CallerSuppliedMetadata};
  const auto bytes = write_class(owner, supplied).value().bytes;
  const auto check = [&](analysis::AnalysisContext& restricted, analysis::SsaErrorKind kind) {
    const auto rejected = analysis::lower_ssa(owner, graph, restricted);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().kind, kind);
    EXPECT_EQ(SsaCommit::source_stamp(owner, index, context), original_stamp);
    EXPECT_EQ(owner.constant_pool().size(), pool_size);
    EXPECT_TRUE(body.code().get(ret));
    EXPECT_EQ(graph.operation(op)->origin, ret);
    EXPECT_EQ(write_class(owner, supplied).value().bytes, bytes);
  };
  analysis::AnalysisContext work(context.hierarchy(), {.max_work = 0});
  check(work, analysis::SsaErrorKind::ResourceLimitExceeded);
  analysis::AnalysisContext memory(context.hierarchy(), {.max_memory_bytes = 1});
  check(memory, analysis::SsaErrorKind::ResourceLimitExceeded);
  std::stop_source stop;
  stop.request_stop();
  analysis::AnalysisContext cancelled(context.hierarchy(), {.cancellation = stop.get_token()});
  check(cancelled, analysis::SsaErrorKind::Cancelled);
  analysis::AnalysisContext other;
  check(other, analysis::SsaErrorKind::StaleSource);
  ASSERT_TRUE(analysis::lower_ssa(owner, graph, context));
  EXPECT_FALSE(body.code().get(ret));
  EXPECT_TRUE(write_class(owner));
}

TEST(SsaCommit, HistoricalLocalLimitRejectsPublicationAndPreservesCodeHandle) {
  auto owner = ClassFile::create({45, 2}, "ShortCode").value();
  const auto index = declare(owner);
  auto& body = owner.add_code(index, owner.constant_pool().append_utf8("Code").value()).value().get();
  const auto old_ret = body.code().append(Instruction::return_void()).value();
  analysis::AnalysisContext context;
  const auto stamp = SsaCommit::source_stamp(owner, index, context);
  SsaCommit tx(owner, index);
  ASSERT_TRUE(tx.code().append(Instruction::simple(Opcode::IConst1).value()));
  ASSERT_TRUE(tx.code().append(Instruction::istore(255)));
  ASSERT_TRUE(tx.code().append(Instruction::return_void()));
  auto rejected = tx.finish(stamp, context, {}, {}, SsaCodeProvenance{}, false);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, WriteErrorKind::InvalidCodeLayout);
  EXPECT_EQ(SsaCommit::source_stamp(owner, index, context), stamp);
  EXPECT_TRUE(body.code().get(old_ret));
}

TEST(SsaCommit, FailedVerificationRollsBackDynamicConstantsAndBootstrapRows) {
  auto owner = ClassFile::create({61, 0}, "AtomicBody").value();
  const auto index = declare(owner);
  auto& source = owner.add_code(index, owner.constant_pool().append_utf8("Code").value()).value().get();
  auto old_id = source.code().append(Instruction::return_void()).value();
  source.supply_metadata(0, 0, {});
  const WriteOptions supplied{.code_policy = CodeWritePolicy::CallerSuppliedMetadata};
  const auto original = write_class(owner, supplied).value();
  const auto pool_size = owner.constant_pool().size();
  analysis::AnalysisContext context;
  const auto stamp = SsaCommit::source_stamp(owner, index, context);
  {
    SsaCommit tx(owner, index);
    const DynamicInvocation call{"constant", "()I", {{MethodHandleKind::InvokeStatic, "Bootstrap", "link", "(Ljava/lang/Object;)Ljava/lang/Object;"}}};
    ASSERT_TRUE(tx.resolve(MethodInstruction{call}));
    ASSERT_GT(owner.constant_pool().size(), pool_size);
    ASSERT_TRUE(tx.code().append(Instruction::simple(Opcode::IReturn).value()));
    const auto failed = tx.finish(stamp, context, {}, {}, SsaCodeProvenance{}, false);
    EXPECT_FALSE(failed);
  }
  EXPECT_EQ(SsaCommit::source_stamp(owner, index, context), stamp);
  EXPECT_TRUE(source.code().get(old_id));
  EXPECT_EQ(owner.constant_pool().size(), pool_size);
  EXPECT_TRUE(owner.attributes().empty());
  EXPECT_EQ(write_class(owner, supplied)->bytes, original.bytes);
}

TEST(SsaCommit, ChangingMetadataWithoutEditingInstructionsMakesSourceStale) {
  auto owner = ClassFile::create({61, 0}, "StaleBody").value();
  const auto index = declare(owner);
  auto& source = owner.add_code(index, owner.constant_pool().append_utf8("Code").value()).value().get();
  const auto id = source.code().append(Instruction::return_void()).value();
  analysis::AnalysisContext context;
  const auto stamp = SsaCommit::source_stamp(owner, index, context);
  source.supply_metadata(0, 0, {});
  SsaCommit tx(owner, index);
  ASSERT_TRUE(tx.code().append(Instruction::return_void()));
  const auto rejected = tx.finish(stamp, context, {}, {}, SsaCodeProvenance{}, false);
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, WriteErrorKind::StaleAnalysisResult);
  EXPECT_TRUE(source.code().get(id));
}

TEST(SsaCommit, PublishedDisconnectedRootSupportsDefaultWritingAndSubsequentLifting) {
  auto owner = ClassFile::create({61, 0}, "DeadRoot").value();
  const auto index = declare(owner);
  analysis::AnalysisContext context;
  SsaCommit tx(owner, index);
  ASSERT_TRUE(tx.code().append(Instruction::return_void()));
  const auto root = tx.code().append(Instruction::return_void()).value();
  analysis::StackMapFrame seed{root, analysis::Frame{}};
  seed.frame.locals.resize(25);
  seed.frame.locals[23].kind = analysis::VerificationKind::Long;
  seed.frame.locals[24].kind = analysis::VerificationKind::WideContinuation;
  seed.frame.stack = {{analysis::VerificationKind::Double}};
  seed.frame.stack_slots = 2;
  auto result = tx.finish(SsaCommit::source_stamp(owner, index, context), context, {},
                          std::span(&seed, 1), SsaCodeProvenance{}, false);
  ASSERT_TRUE(result) << result.error().detail;
  auto* installed = owner.edit_code(index);
  EXPECT_EQ(installed->max_locals(), 25u);
  EXPECT_EQ(installed->max_stack(), 2u);
  auto declared = analysis::decode_ssa_frames(owner, owner.methods()[index], context.limits());
  ASSERT_TRUE(declared) << declared.error().detail;
  ASSERT_EQ(declared->entries.size(), 1u);
  EXPECT_EQ(declared->entries.front().instruction, installed->code().at(1));
  EXPECT_EQ(declared->entries.front().frame.stack_slots, 2u);
  auto ordinary = write_class(owner);
  ASSERT_TRUE(ordinary) << ordinary.error().detail;
  auto computed = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context);
  ASSERT_TRUE(computed) << computed.error().detail;
  EXPECT_EQ(computed->bytes, ordinary->bytes);
  auto parsed = read_class(ordinary->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(parsed) << parsed.error().detail;
}

TEST(SsaCommit, PreservesSiblingLegacyCodeAndConcreteLongConditionalEncoding) {
  auto owner = ClassFile::create({50, 0}, "Mixed").value();
  const auto legacy_index = declare(owner, "legacy");
  const auto index = declare(owner, "run", "(I)V");
  const auto code_name = owner.constant_pool().append_utf8("Code").value();
  auto& legacy = owner.add_code(legacy_index, code_name).value().get();
  auto subroutine = legacy.code().make_label().value();
  ASSERT_TRUE(legacy.code().append(Instruction::branch(Opcode::Jsr, subroutine).value()));
  ASSERT_TRUE(legacy.code().append(Instruction::return_void()));
  auto sub_start = legacy.code().append(Instruction::astore(0)).value();
  ASSERT_TRUE(legacy.code().bind_label(subroutine, CodePosition{sub_start}));
  ASSERT_TRUE(legacy.code().append(Instruction::local(Opcode::Ret, 0).value()));
  legacy.supply_metadata(1, 1, {});
  auto& early = owner.add_code(index, code_name).value().get();
  ASSERT_TRUE(early.code().append(Instruction::return_void()));
  early.supply_metadata(0, 1, {});
  const auto initial = write_class(owner, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata}).value();
  auto parsed = read_class(initial.bytes, {.mode = ParseMode::Strict}).value();
  auto& file = parsed.class_file;
  const auto& body = file.methods()[index].code()->code();
  const auto old_handle = body.at(0);
  analysis::AnalysisContext context;
  SsaCommit tx(file, index);
  auto& staged = tx.code();
  auto end = staged.make_label().value();
  ASSERT_TRUE(staged.append(Instruction::iload(0)));
  ASSERT_TRUE(staged.append(Instruction::branch(Opcode::IfEq, end).value()));
  std::vector<InstructionBuilder> padding(32768, Instruction::nop());
  ASSERT_TRUE(staged.append(padding));
  const auto target = staged.append(Instruction::return_void()).value();
  ASSERT_TRUE(staged.bind_label(end, CodePosition{target}));
  const auto committed = tx.finish(SsaCommit::source_stamp(file, index, context), context, {}, {}, SsaCodeProvenance{}, false);
  ASSERT_TRUE(committed) << committed.error().detail;
  EXPECT_FALSE(body.get(old_handle));
  EXPECT_EQ(body.get(body.at(1))->opcode(), Opcode::IfNe);
  EXPECT_EQ(body.get(body.at(2))->opcode(), Opcode::Goto);
  const auto emitted = write_class(file);
  ASSERT_TRUE(emitted) << emitted.error().detail;
  const auto forced = write_class(file, {.force_reencode = true});
  ASSERT_TRUE(forced) << forced.error().detail;
  EXPECT_EQ(forced->bytes, emitted->bytes);
  const auto final = read_class(emitted->bytes, {.mode = ParseMode::Strict});
  ASSERT_TRUE(final) << final.error().detail;
  EXPECT_EQ(final->class_file.methods()[legacy_index].code()->code().size(), 4u);
  EXPECT_EQ(final->class_file.methods()[index].code()->attributes().front().kind(), AttributeKind::StackMapTable);
}

} // namespace
} // namespace cafe::detail
