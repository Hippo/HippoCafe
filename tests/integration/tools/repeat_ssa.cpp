#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"
#include "io/binary_reader.hpp"
#include "io/code_encoder.hpp"

namespace {

using namespace cafe;
using namespace cafe::analysis;
using std::filesystem::path;

template<typename T, typename E>
T checked(std::expected<T, E> result) {
  if (!result)
    throw std::runtime_error(result.error().detail);
  if constexpr (!std::is_void_v<T>)
    return std::move(*result);
}

template<typename T>
T required(std::optional<T> value, std::string_view detail) {
  if (!value)
    throw std::runtime_error(std::string(detail));
  return std::move(*value);
}

std::uint16_t field(detail::BinaryReader& reader) {
  return required(reader.u2(), "truncated debug attribute");
}

void save(const path& directory, std::span<const std::byte> bytes) {
  std::filesystem::create_directories(directory);
  const auto result = examples::save_class((directory / "RepeatedSsaLoop.class").string().c_str(), bytes);
  if (!result)
    throw std::runtime_error(std::string(result.error()));
}

void edit_addend(SsaMethod& graph) {
  for (const auto& block : graph.blocks()) {
    for (const auto id : block.operations) {
      if (graph.operation(id)->opcode == Opcode::IAdd) {
        const auto two = checked(graph.insert_before(id, Instruction::simple(Opcode::IConst2).value(),
                                                     {}, SsaType{VerificationKind::Integer}));
        checked(graph.set_input(id, 1, graph.operation(two)->result));
        return;
      }
    }
  }
  throw std::runtime_error("missing addition to edit");
}

std::optional<std::uint16_t> visible_slot(const ClassFile& owner, const CodeAttribute& code, std::uint32_t pc,
                                          std::string_view name) {
  std::optional<std::uint16_t> found;
  for (const auto& attribute : code.attributes()) {
    if (attribute.kind() != AttributeKind::LocalVariableTable)
      continue;
    detail::BinaryReader reader(required(attribute.get<RawAttribute>()->bytes(owner), "missing local table bytes"));
    const auto count = field(reader);
    for (unsigned row = 0; row < count; ++row) {
      const auto start = field(reader);
      const auto length = field(reader);
      const auto local_name = owner.constant_pool().get_utf8(CpIndex{field(reader)});
      const auto type = owner.constant_pool().get_utf8(CpIndex{field(reader)});
      const auto slot = field(reader);
      if (local_name == name && type == "I" && start <= pc && pc < std::uint32_t{start} + length) {
        if (found)
          throw std::runtime_error("ambiguous debug local " + std::string(name));
        found = slot;
      }
    }
  }
  return found;
}

// Which source variables would a debugger show when stopping on each loop line?
std::map<unsigned, std::pair<bool, bool>> line_variables(const ClassFile& owner, const CodeAttribute& code) {
  std::map<unsigned, std::pair<bool, bool>> variables;
  for (const auto& attr : code.attributes()) {
    if (attr.kind() != AttributeKind::LineNumberTable)
      continue;
    detail::BinaryReader reader(required(attr.get<RawAttribute>()->bytes(owner), "missing line table bytes"));
    const auto count = field(reader);
    for (unsigned row = 0; row < count; ++row) {
      const auto pc = field(reader);
      const auto line = field(reader);
      variables.emplace(line, std::pair{visible_slot(owner, code, pc, "n").has_value(),
                                        visible_slot(owner, code, pc, "total").has_value()});
    }
  }
  return variables;
}

// Read the advertised slots on each visit to the condition, with the real JVM
// checking the values. This copy is never fed back into the rewrite loop.
void add_debug_probe(ClassFile& owner, std::size_t index, AnalysisContext& context, const path& output) {
  auto editor = required(owner.edit_method(index), "missing sum method");
  auto& attribute = *editor.code();
  auto& code = attribute.code();
  std::pmr::monotonic_buffer_resource scratch;
  const auto layout = checked(detail::encode_code(code, owner.version(), owner.constant_pool(), attribute.max_locals(),
                                                  true, &scratch, "sum"));
  for (std::size_t i = 0; i < code.size(); ++i) {
    const auto branch = code.at(i);
    const auto original = code.get(branch).value();
    if (opcode_info(original.opcode()).control_flow != ControlFlow::ConditionalBranch)
      continue;
    const auto target = code.branch_target(branch).value();
    const auto pc = layout.instruction_offsets()[i];
    const auto n = required(visible_slot(owner, attribute, pc, "n"), "n is out of scope at loop condition");
    const auto total = required(visible_slot(owner, attribute, pc, "total"), "total is out of scope at loop condition");
    // Keep all incoming labels attached to the start of the probe.
    const auto next = code.at(i + 1);
    code.replace(branch, Instruction::iload(n)).value();
    code.insert_before(next, Instruction::iload(total)).value();
    editor.insert_before(next, MethodInvocation{Opcode::InvokeStatic, "VerifyRepeatedSsaLoop", "observe", "(II)V"}).value();
    code.insert_before(next, Branch{original.opcode(), target}).value();
    const auto emitted = checked(write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context));
    save(output, emitted.bytes);
    return;
  }
  throw std::runtime_error("missing loop condition for debugger probe");
}

} // namespace

int main(int argc, char** argv) try {
  if (argc != 3)
    throw std::runtime_error("usage: repeat_ssa output-directory debug|plain");
  const path out = argv[1];
  auto input = examples::load_class((out / "original" / "RepeatedSsaLoop.class").string().c_str());
  if (!input)
    throw std::runtime_error(std::string(input.error()));
  auto bytes = std::move(*input);
  AnalysisContext context;
  const bool debug = std::string_view(argv[2]) == "debug";
  std::map<unsigned, std::pair<bool, bool>> original_variables;
  std::pair<std::size_t, std::uint16_t> after_edit{};
  bool stable = true;
  bool visible = true;
  for (int round = 0; round <= 5; ++round) {
    auto owner = checked(read_class(bytes, {.mode = ParseMode::Strict})).class_file;
    const auto index = required(owner.find_method("sum", "(I)I"), "missing sum method");
    const auto& code = *owner.methods()[index].code();
    const auto size = std::pair{code.code().size(), code.max_locals()};
    std::cout << "round " << round << ": " << size.first << " instructions, " << size.second << " locals\n";
    if (debug) {
      const auto variables = line_variables(owner, code);
      if (round == 0)
        original_variables = variables;
      for (const auto& [line, names] : original_variables) {
        const auto found = variables.find(line);
        if (found == variables.end() || found->second != names) {
          std::cerr << "round " << round << ": visible locals changed on source line " << line << '\n';
          visible = false;
        }
      }
    }
    if (round == 1)
      after_edit = size;
    if (round > 1 && size != after_edit)
      stable = false;
    if (round == 5) {
      if (debug)
        add_debug_probe(owner, index, context, out / "probe");
      break;
    }
    auto graph = checked(lift_ssa(owner, index, context));
    if (round == 0)
      edit_addend(graph);
    auto result = checked(lower_ssa(owner, graph, context));
    if (!result.diagnostics.empty())
      throw std::runtime_error("lowering lost loop metadata");
    bytes = checked(write_class(owner)).bytes;
    save(out / std::to_string(round + 1), bytes);
  }
  if (!stable)
    throw std::runtime_error("unchanged SSA rounds changed instruction count or max_locals");
  if (!visible)
    throw std::runtime_error("debug variable scopes changed at source lines");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
