#include <array>
#include <memory_resource>

#include <gtest/gtest.h>

#include "bytecode/code_access.hpp"

namespace cafe {
namespace {

using detail::BytecodeAccess;
using detail::CodePatch;
using detail::PatchPlacement;

TEST(CodePatch, RedirectsPreexistingExplicitEdgesAndKeepsSharedBoundaries) {
  MethodCode code;
  const auto anchor = code.append(Instruction::return_void()).value();
  const auto shared = code.make_label().value();
  const auto alias = code.make_label().value();
  code.bind_label(shared, {anchor}).value();
  code.bind_label(alias, {anchor}).value();
  const auto branch = code.append(Branch{Opcode::IfNull, shared}).value();
  const auto other = code.make_label().value();
  code.bind_label(other, {branch}).value();
  const std::array targets{alias, other, shared};
  const auto table = code.append(TableSwitch{-2, alias, targets}).value();
  const std::array pairs{LookupPair{1, shared}, LookupPair{8, other}};
  const auto lookup = code.append(LookupSwitch{shared, pairs}).value();
  const auto table_id = code.switch_id(table).value();
  const auto lookup_id = code.switch_id(lookup).value();
  const auto revision = code.revision();
  const std::array<InstructionBuilder, 2> block{Instruction::nop(), Branch{Opcode::Goto, alias}};
  const auto result = BytecodeAccess::apply_patch(code, {.anchor = anchor, .include_incoming = true}, block);
  ASSERT_TRUE(result);
  EXPECT_EQ(code.revision(), revision + 1);
  const auto entry = code.branch_target(branch).value();
  EXPECT_EQ(code.label_position(entry)->before, result->front());
  EXPECT_EQ(code.label_position(shared)->before, anchor);
  EXPECT_EQ(code.label_position(alias)->before, anchor);
  EXPECT_EQ(code.branch_target(result->back()), alias);
  const auto dense = code.table_switch(table_id).value();
  EXPECT_EQ(dense.default_target, entry);
  EXPECT_EQ(dense.targets[0], entry);
  EXPECT_EQ(dense.targets[1], other);
  EXPECT_EQ(dense.targets[2], entry);
  const auto sparse = code.lookup_switch(lookup_id).value();
  EXPECT_EQ(sparse.default_target, entry);
  EXPECT_EQ(sparse.pairs[0].target, entry);
  EXPECT_EQ(sparse.pairs[1].target, other);
  const std::array match{branch};
  const auto changed = BytecodeAccess::match_position(code, match, BytecodeAccess::scope(code), revision);
  EXPECT_EQ(changed.error().cause, CodeEditError::StaleMatch);
}

TEST(CodePatch, ResolvesLocalTargetsAtBothEndsAndRejectsBranchesPastMethodEnd) {
  MethodCode code;
  const auto anchor = code.append(Instruction::nop()).value();
  const auto successor = code.append(Instruction::return_void()).value();
  const std::array<InstructionBuilder, 3> block{Branch{Opcode::IfEq, {}}, Instruction::nop(), Branch{Opcode::Goto, {}}};
  const std::array<std::optional<std::size_t>, 2> bindings{3u, 0u};
  const std::array<std::pair<std::size_t, std::size_t>, 2> fixups{{{0, 0}, {2, 1}}};
  CodePatch patch{.placement = PatchPlacement::After, .anchor = successor, .label_bindings = bindings, .branch_fixups = fixups};
  const auto revision = code.revision();
  auto failed = BytecodeAccess::apply_patch(code, patch, block);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().cause, CodeEditError::InvalidLabel);
  EXPECT_EQ(failed.error().instruction_index, 0u);
  EXPECT_EQ(BytecodeAccess::label_count(code), 0u);
  EXPECT_EQ(code.revision(), revision);
  patch.anchor = anchor;
  const auto installed = BytecodeAccess::apply_patch(code, patch, block).value();
  EXPECT_EQ(code.label_position(code.branch_target(installed.front()).value())->before, successor);
  EXPECT_EQ(code.label_position(code.branch_target(installed.back()).value())->before, installed.front());
  EXPECT_EQ(code.revision(), revision + 1);
}

TEST(CodePatch, ResolvesDenseAndSparseSwitchesAgainstTheSameBuilderLabels) {
  MethodCode code;
  const auto anchor = code.append(Instruction::return_void()).value();
  const std::array<LabelId, 2> targets{};
  std::array<LookupPair, 2> pairs{{{13, {}}, {2, {}}}};
  const std::array<InstructionBuilder, 2> block{TableSwitch{-1, {}, targets}, LookupSwitch{{}, pairs}};
  const std::array<std::optional<std::size_t>, 3> bindings{0u, 1u, 2u};
  const std::array<std::pair<std::size_t, std::size_t>, 6> fixups{{{0, 2}, {0, 0}, {0, 1}, {1, 0}, {1, 2}, {1, 1}}};
  const CodePatch patch{.anchor = anchor, .label_bindings = bindings, .branch_fixups = fixups};
  const auto previous_revision = code.revision();
  auto bad = BytecodeAccess::apply_patch(code, patch, block);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().cause, CodeEditError::InvalidSwitch);
  EXPECT_EQ(bad.error().instruction_index, 1u);
  EXPECT_EQ(BytecodeAccess::label_count(code), 0u);
  EXPECT_EQ(code.revision(), previous_revision);

  pairs.front().key = -13;
  const auto ids = BytecodeAccess::apply_patch(code, patch, block).value();
  EXPECT_EQ(BytecodeAccess::label_count(code), 3u);
  const auto table = code.table_switch(code.switch_id(ids[0]).value()).value();
  const auto lookup = code.lookup_switch(code.switch_id(ids[1]).value()).value();
  EXPECT_EQ(table.low, -1);
  EXPECT_EQ(code.label_position(table.default_target)->before, anchor);
  EXPECT_EQ(code.label_position(table.targets[0])->before, ids[0]);
  EXPECT_EQ(code.label_position(table.targets[1])->before, ids[1]);
  EXPECT_EQ(lookup.default_target, table.targets[0]);
  EXPECT_EQ(lookup.pairs[0], (LookupPair{-13, table.default_target}));
  EXPECT_EQ(lookup.pairs[1], (LookupPair{2, table.targets[1]}));
}

class FailingResource final : public std::pmr::memory_resource {
public:
  std::optional<std::size_t> remaining;

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (remaining) {
      if (*remaining == 0)
        throw std::bad_alloc();
      --*remaining;
    }
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }

  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }

  bool do_is_equal(const std::pmr::memory_resource& resource) const noexcept override {
    return this == &resource;
  }
};

TEST(CodePatch, AllocationFailureDuringStagingPreservesEdgesAndFreeSlots) {
  for (std::size_t budget = 0; budget < 32; ++budget) {
    FailingResource resource;
    MethodCode code(&resource);
    const auto anchor = code.append(Instruction::return_void()).value();
    const auto original = code.make_label().value();
    code.bind_label(original, {anchor}).value();
    const auto branch = code.append(Branch{Opcode::Goto, original}).value();
    const auto free_slot = code.append(Instruction::nop()).value();
    code.erase(free_slot).value();
    const auto revision = code.revision();
    const std::array targets{original};
    const std::array<InstructionBuilder, 4> block{TableSwitch{0, original, targets}, Instruction::nop(),
                                                  Branch{Opcode::Goto, {}}, Instruction::nop()};
    const std::array<std::optional<std::size_t>, 1> binding{1u};
    const std::array<std::pair<std::size_t, std::size_t>, 1> fixup{{{2, 0}}};
    const CodePatch patch{.anchor = anchor, .include_incoming = true, .label_bindings = binding, .branch_fixups = fixup};
    resource.remaining = budget;
    try {
      const auto result = BytecodeAccess::apply_patch(code, patch, block);
      ASSERT_TRUE(result);
      EXPECT_EQ(result->front().index, free_slot.index);
      EXPECT_NE(result->front().generation, free_slot.generation);
      EXPECT_EQ(code.revision(), revision + 1);
      return;
    } catch (const std::bad_alloc&) {
      resource.remaining.reset();
      EXPECT_EQ(code.size(), 2u);
      EXPECT_EQ(code.revision(), revision);
      EXPECT_EQ(code.at(0), anchor);
      EXPECT_EQ(code.at(1), branch);
      EXPECT_EQ(code.branch_target(branch), original);
      EXPECT_EQ(code.label_position(original)->before, anchor);
      EXPECT_EQ(BytecodeAccess::label_count(code), 1u);
      EXPECT_FALSE(code.get(free_slot));
      EXPECT_EQ(code.append(Instruction::nop()).value().index, free_slot.index);
    }
  }
  FAIL() << "No allocation budget allowed the patch to commit";
}

} // namespace
} // namespace cafe
