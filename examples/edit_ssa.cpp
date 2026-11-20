/**
 * @file
 * @brief Change the operands of an integer subtraction through its SSA definition.
 */

/** @cond example_programs */
#include <array>
#include <expected>
#include <iostream>
#include <string>
#include <utility>

#include <hippo/cafe.hpp>

// [edit-ssa]
std::expected<cafe::WriteResult, std::string> reverse_difference(
    cafe::ClassFile& owner, std::size_t method, cafe::analysis::AnalysisContext& context) {
  auto graph = cafe::analysis::lift_ssa(owner, method, context);
  if (!graph)
    return std::unexpected(std::move(graph.error().detail));
  for (const auto& block : graph->blocks()) {
    for (const auto id : block.operations) {
      const auto* op = graph->operation(id);
      if (op->opcode != cafe::Opcode::ISub)
        continue;
      const auto left = op->inputs[0];
      const auto right = op->inputs[1];
      if (auto edit = graph->set_input(id, 0, right); !edit)
        return std::unexpected(std::move(edit.error().detail));
      if (auto edit = graph->set_input(id, 1, left); !edit)
        return std::unexpected(std::move(edit.error().detail));
      if (auto lowered = cafe::analysis::lower_ssa(owner, *graph, context); !lowered)
        return std::unexpected(std::move(lowered.error().detail));
      return cafe::write_class(owner).transform_error([](cafe::WriteError error) {
        return std::move(error.detail);
      });
    }
  }
  return std::unexpected("method contains no integer subtraction");
}
// [edit-ssa]

int main() {
  auto owner = cafe::ClassFile::create({61, 0}, "Arithmetic");
  if (!owner)
    return 1;
  auto& pool = owner->constant_pool();
  const auto name = pool.append_utf8("difference");
  const auto descriptor = pool.append_utf8("(II)I");
  const auto code_name = pool.append_utf8("Code");
  if (!name || !descriptor || !code_name)
    return 1;
  const auto method = owner->add_method(0x0009, *name, *descriptor);
  if (!method)
    return 1;
  auto body = owner->add_code(*method, *code_name);
  if (!body)
    return 1;
  const auto subtract = cafe::Instruction::simple(cafe::Opcode::ISub);
  const auto return_result = cafe::Instruction::simple(cafe::Opcode::IReturn);
  if (!subtract || !return_result)
    return 1;
  const std::array<cafe::InstructionBuilder, 4> code{
      cafe::Instruction::iload(0),
      cafe::Instruction::iload(1),
      *subtract,
      *return_result,
  };
  if (!body->get().code().append(code))
    return 1;

  cafe::analysis::AnalysisContext context;
  auto written = reverse_difference(*owner, *method, context);
  if (!written) {
    std::cerr << written.error() << '\n';
    return 1;
  }
  std::cout << written->bytes.size() << " bytes written after swapping the subtraction operands\n";
}
/** @endcond */
