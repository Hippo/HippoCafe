#include "properties.hpp"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

namespace cafe::fuzz {
namespace {

using namespace analysis;

const ReadOptions STRICT_INPUT{
    .mode = ParseMode::Strict,
    .limits = {.max_input_bytes = MAX_INPUT_BYTES, .max_total_allocated_bytes = 16 * 1024 * 1024, .max_attributes = 10'000, .max_methods = 128}};

AnalysisLimits ssa_limits() {
  AnalysisLimits limits;
  limits.max_instructions = 2048;
  limits.max_handlers = 64;
  limits.max_edges = 8192;
  limits.max_work = 131'072;
  limits.max_memory_bytes = 4 * 1024 * 1024;
  limits.max_stack_slots = 512;
  limits.max_local_slots = 512;
  return limits;
}

[[noreturn]] void fail(const char* stage, const std::string& detail) {
  std::fprintf(stderr, "%s: %s\n", stage, detail.c_str());
  std::abort();
}

template<class T>
T require(std::expected<T, SsaError> result, const char* stage) {
  if (!result)
    fail(stage, result.error().detail);
  return std::move(result).value();
}

SsaOperationId subtraction(const SsaMethod& graph) {
  for (const auto& block : graph.blocks()) {
    for (const auto id : block.operations) {
      if (graph.operation(id)->opcode == Opcode::ISub)
        return id;
    }
  }
  fail("lookup generated subtraction", "operation missing");
}

// Each property serializes the installed body through the ordinary writer. Reopen with
// enough room for the additional instructions and metadata produced by lowering.
void check_rewrite(const ClassFile& owner, std::size_t method, AnalysisContext& context, bool edited_program = false) {
  const auto written = write_class(owner);
  if (!written)
    fail("write lowered SSA", written.error().detail);
  auto options = STRICT_INPUT;
  options.limits.max_input_bytes *= 2;
  options.limits.max_total_allocated_bytes *= 2;
  options.limits.max_attributes *= 2;
  auto reopened = read_class(written->bytes, options);
  if (!reopened)
    fail("strictly read lowered SSA", reopened.error().detail);
  auto graph = require(lift_ssa(reopened->class_file, method, context), "re-lift lowered SSA");
  require(verify_ssa(reopened->class_file, graph, context), "verify re-lifted SSA");
  if (edited_program) {
    const auto& operands = graph.operation(subtraction(graph))->inputs;
    if (graph.parameters().size() != 3 || operands.size() != 2 ||
        operands[0] != graph.parameters()[2] || operands[1] != graph.parameters()[1])
      fail("re-lift edited subtraction", "operand edit lost in bytecode");
  }
}

// The leading subtraction consumes the first two parameters until the test edits it.
// Each byte adds an immediate or a diamond carrying a nonempty operand stack.
ClassFile make_program(std::string_view input) {
  auto owner = ClassFile::create({65, 0}, "SsaFuzz").value();
  auto& pool = owner.constant_pool();
  const auto method = owner.add_method(0x0009, pool.append_utf8("f").value(), pool.append_utf8("(III)I").value()).value();
  auto& code = owner.add_code(method, pool.append_utf8("Code").value()).value().get().code();
  code.append(Instruction::iload(0)).value();
  code.append(Instruction::iload(1)).value();
  code.append(Instruction::simple(Opcode::ISub).value()).value();
  for (const unsigned char byte : input) {
    const auto constant = Instruction::bipush(static_cast<std::int8_t>(byte & 127));
    if ((byte & 1U) != 0) {
      const auto alternate = code.make_label().value();
      const auto join = code.make_label().value();
      code.append(Instruction::iload(1)).value();
      code.append(Instruction::branch(Opcode::IfEq, alternate).value()).value();
      code.append(constant).value();
      code.append(Instruction::branch(Opcode::Goto, join).value()).value();
      const auto alternate_value = code.append(Instruction::iload(0)).value();
      code.bind_label(alternate, CodePosition{alternate_value}).value();
      const auto add = code.append(Instruction::simple(Opcode::IAdd).value()).value();
      code.bind_label(join, CodePosition{add}).value();
    } else {
      code.append(constant).value();
      code.append(Instruction::simple(Opcode::IXor).value()).value();
    }
  }
  code.append(Instruction::simple(Opcode::IReturn).value()).value();
  return owner;
}

} // namespace

void exercise_ssa(std::string_view input) {
  auto parsed = read_class(std::as_bytes(std::span(input)), STRICT_INPUT);
  if (!parsed || !write_class(parsed->class_file))
    return;
  AnalysisContext context(ssa_limits());
  auto& owner = parsed->class_file;
  const auto method_count = owner.methods().size();
  std::size_t attempted = 0;
  for (std::size_t offset = 0; offset < method_count && attempted < 4; ++offset) {
    // Length mutations rotate the bounded sample across larger method tables.
    const auto i = (input.size() + offset) % method_count;
    if (!owner.methods()[i].code())
      continue;
    ++attempted;
    auto graph = lift_ssa(owner, i, context);
    if (!graph)
      continue;
    const auto checked = verify_ssa(owner, *graph, context);
    if (!checked && checked.error().kind == SsaErrorKind::ResourceLimitExceeded)
      continue;
    if (!checked)
      fail("verify lifted SSA", checked.error().detail);
    const auto lowered = lower_ssa(owner, *graph, context, {.drop_unsafe_code_attributes = true});
    if (!lowered) {
      if (!graph->is_current(owner, context))
        fail("failed lowering changed the source", lowered.error().detail);
      continue;
    }
    // Emitted code can have more instructions and locals than the bounded input.
    AnalysisContext output_context;
    check_rewrite(owner, i, output_context);
  }
}

void exercise_ssa_program(std::string_view input) {
  if (input.size() > MAX_PROGRAM_BYTES)
    return;
  auto owner = make_program(input);
  AnalysisContext context(ssa_limits());
  if (!input.empty() && (static_cast<unsigned char>(input.front()) & 7U) == 1U) {
    auto& code = owner.edit_code(0)->code();
    const auto pop = code.insert_before(code.at(0), Instruction::simple(Opcode::Pop).value()).value();
    const auto rejected = lift_ssa(owner, 0, context);
    if (rejected || !rejected.error().analysis || rejected.error().analysis->kind != AnalysisErrorKind::StackUnderflow)
      fail("lift underflowing method", "missing structured stack underflow");
    code.erase(pop).value();
  }
  auto graph = require(lift_ssa(owner, 0, context), "lift generated method");
  const auto sub = subtraction(graph);
  const auto invalid = graph.set_input(sub, 0, {});
  if (invalid || invalid.error().kind != SsaErrorKind::InvalidHandle)
    fail("invalid operand handle", "missing structured rejection");
  require(graph.set_input(sub, 0, graph.parameters()[2]), "edit generated subtraction");
  require(verify_ssa(owner, graph, context), "verify edited method");
  require(lower_ssa(owner, graph, context), "lower edited method");
  check_rewrite(owner, 0, context, true);
}

} // namespace cafe::fuzz
