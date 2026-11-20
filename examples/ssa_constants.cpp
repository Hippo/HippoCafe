/**
 * @file
 * @brief Propagate integer constants through a chain of additions using indexed SSA uses.
 */

/** @cond example_programs */
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <hippo/cafe.hpp>

namespace {
using namespace cafe;
using namespace cafe::analysis;

std::optional<std::int32_t> small_integer(const Instruction& instruction) {
  const auto opcode = instruction.opcode();
  if (opcode >= Opcode::IConstM1 && opcode <= Opcode::IConst5)
    return static_cast<int>(opcode) - static_cast<int>(Opcode::IConst0);
  return instruction.immediate_value(); // bipush or sipush
}

// Discover folds without editing the graph. Each new constant wakes up its consumers.
SsaResult<std::size_t> fold_sums(SsaMethod& graph, const SsaAnalysis& analysis) {
  std::map<std::uint32_t, std::int32_t> constants;
  std::vector<SsaValueId> work;
  std::vector<std::pair<SsaOperationId, std::int16_t>> folds;
  for (const auto& block : graph.blocks()) {
    for (const auto id : block.operations) {
      const auto& operation = *graph.operation(id);
      if (const auto literal = small_integer(operation.instruction)) {
        constants.emplace(operation.result.index, *literal);
        work.push_back(operation.result);
      }
    }
  }
  while (!work.empty()) {
    const auto value = work.back();
    work.pop_back();
    const auto uses = analysis.uses(value);
    if (!uses)
      return std::unexpected(uses.error());
    for (const auto& use : *uses) {
      const auto* user = graph.operation(use.operation);
      if (!user || use.local_assignment || user->opcode != Opcode::IAdd)
        continue;
      const auto left = constants.find(user->inputs[0].index);
      const auto right = constants.find(user->inputs[1].index);
      if (left == constants.end() || right == constants.end())
        continue;
      // Both inputs fit in a signed short. Only fold sums encodable by sipush.
      const auto sum = left->second + right->second;
      if (std::in_range<std::int16_t>(sum) && constants.emplace(user->result.index, sum).second) {
        work.push_back(user->result);
        folds.emplace_back(user->id, static_cast<std::int16_t>(sum));
      }
    }
  }

  // The first insertion invalidates the snapshot. The saved operation IDs survive.
  for (const auto& [id, sum] : folds) {
    const auto old = graph.operation(id)->result;
    const auto literal = graph.insert_before(id, Instruction::sipush(sum), {}, SsaType{VerificationKind::Integer});
    if (!literal)
      return std::unexpected(literal.error());
    if (const auto edit = graph.replace_uses(old, graph.operation(*literal)->result); !edit)
      return std::unexpected(edit.error());
    if (const auto edit = graph.erase_operation(id); !edit)
      return std::unexpected(edit.error());
  }
  return folds.size();
}
} // namespace

int main() try {
  auto owner = ClassFile::create({61, 0}, "Arithmetic").value();
  auto& pool = owner.constant_pool();
  const auto method = owner.add_method(0x0009, pool.append_utf8("answer").value(), pool.append_utf8("()I").value()).value();
  auto& body = owner.add_code(method, pool.append_utf8("Code").value()).value().get();
  const auto add = Instruction::simple(Opcode::IAdd).value();
  const std::array<InstructionBuilder, 6> code{
      Instruction::bipush(60), Instruction::bipush(-17), add,
      Instruction::simple(Opcode::IConstM1).value(), add, Instruction::simple(Opcode::IReturn).value()};
  body.code().append(code).value();

  AnalysisContext context;
  auto graph = lift_ssa(owner, method, context).value();
  const auto analysis = analyze_ssa(owner, graph, context).value();
  const auto folded = fold_sums(graph, analysis);
  if (!folded) {
    std::cerr << folded.error().detail << '\n';
    return 1;
  }
  lower_ssa(owner, graph, context).value();
  const auto bytes = write_class(owner).value().bytes;
  const auto parsed = read_class(bytes, {.mode = ParseMode::Strict}).value();
  const auto rewritten = lift_ssa(parsed.class_file, method, context).value();
  const auto& returned = *rewritten.operation(rewritten.block(rewritten.entry_block())->operations.back());
  if (*folded != 2 || returned.opcode != Opcode::IReturn)
    return 1;
  const auto* literal = rewritten.operation(rewritten.value(returned.inputs.front())->operation);
  if (!literal || small_integer(literal->instruction) != 42)
    return 1;
  std::cout << "Folded two additions. The rewritten method returns 42.\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
/** @endcond */
