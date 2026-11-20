#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <cafe/analysis/ssa.hpp>
#include <hippo/cafe.hpp>

#include "class_file_io.hpp"
#include "io/binary_reader.hpp"

namespace {

using namespace cafe;
using namespace cafe::analysis;
using std::filesystem::path;

void require(bool condition, std::string_view reason) {
  if (!condition)
    throw std::runtime_error(std::string(reason));
}

template<typename T, typename Error>
T checked(std::expected<T, Error> result, std::string_view action) {
  if (!result)
    throw std::runtime_error(std::string(action) + ": " + result.error().detail);
  if constexpr (!std::is_void_v<T>)
    return std::move(*result);
}

std::vector<std::byte> read_bytes(const path& file) {
  auto bytes = examples::load_class(file.string().c_str());
  if (!bytes)
    throw std::runtime_error(file.string() + ": " + std::string(bytes.error()));
  return std::move(*bytes);
}

ClassFile parse(std::span<const std::byte> bytes) {
  auto result = checked(read_class(bytes, {.mode = ParseMode::Strict}), "read fixture");
  return std::move(result.class_file);
}

void save(const path& file, std::span<const std::byte> bytes) {
  const auto result = examples::save_class(file.string().c_str(), bytes);
  if (!result)
    throw std::runtime_error(file.string() + ": " + std::string(result.error()));
}

std::size_t declare_method(ClassFile& owner, std::string_view name, std::string_view descriptor = "()I") {
  auto& pool = owner.constant_pool();
  return owner.add_method(0x0009, pool.append_utf8(name).value(), pool.append_utf8(descriptor).value()).value();
}

void install(ClassFile& owner, const SsaMethod& graph, AnalysisContext& context, std::string_view method_name) {
  checked(verify_ssa(owner, graph, context), method_name);
  const auto result = checked(lower_ssa(owner, graph, context), method_name);
  require(result.diagnostics.empty(), "unexpected loss of method metadata");
}

void insert_nop(SsaMethod& graph) {
  const auto first = graph.block(graph.entry_block())->operations.front();
  checked(graph.insert_before(first, Instruction::nop()), "insert SSA nop");
}

void change_return(SsaMethod& graph) {
  const auto entry = graph.entry_block();
  const auto end = graph.block(entry)->operations.back();
  require(graph.operation(end)->opcode == Opcode::IReturn, "editable() must return an int");
  const auto five = checked(graph.insert_before(end, Instruction::simple(Opcode::IConst5).value(),
                                                {}, SsaType{VerificationKind::Integer}),
                            "insert constant");
  checked(graph.set_input(end, 0, graph.operation(five)->result), "edit return operand");
}

void authored_method(ClassFile& owner, AnalysisContext& context) {
  const auto index = declare_method(owner, "authored");
  auto graph = checked(create_ssa(owner, index, context), "create authored graph");
  const auto entry = graph.entry_block();
  const auto two = checked(graph.append_operation(entry, Instruction::simple(Opcode::IConst2).value(),
                                                  {}, SsaType{VerificationKind::Integer}),
                           "append constant");
  const std::array value{graph.operation(two)->result};
  checked(graph.append_terminator(entry, Opcode::IReturn, value), "append return");
  install(owner, graph, context, "authored");
}

void check_catch_annotation(const ClassFile& owner) {
  const auto index = owner.find_method("protectedLocals", "(I)I").value();
  const auto& code = *owner.methods()[index].code();
  const auto attrs = code.attributes();
  const auto annotation = std::ranges::find(attrs, AttributeKind::RuntimeVisibleTypeAnnotations, &Attribute::kind);
  require(annotation != attrs.end(), "missing catch type annotation");
  const auto& pool = owner.constant_pool();
  std::vector<std::size_t> annotated, expected;
  const auto handlers = code.exception_handlers();
  for (std::size_t i = 0; i < handlers.size(); ++i) {
    const auto* type = pool.get(handlers[i].catch_type);
    if (type && type->tag == CpTag::Class && pool.get_utf8(type->first) == "java/lang/RuntimeException")
      expected.push_back(i);
  }
  require(!expected.empty(), "missing runtime exception catch clause");
  cafe::detail::BinaryReader reader(annotation->get<RawAttribute>()->bytes(owner).value());
  const auto count = reader.u2().value();
  for (unsigned i = 0; i < count; ++i) {
    require(reader.u1() == 0x42, "expected an exception_parameter_target annotation");
    annotated.push_back(reader.u2().value());
    require(reader.u1() == 0 && pool.get_utf8(CpIndex{reader.u2().value()}) == "LSsaFixture$Mark;" &&
                reader.u2() == 0,
            "catch annotation payload changed");
  }
  std::ranges::sort(annotated);
  require(reader.remaining() == 0 && annotated == expected, "annotation must follow every emitted row of its catch clause");
}

void reconstruct(ClassFile& owner, AnalysisContext& context, std::optional<std::size_t> editable = {}) {
  for (std::size_t index = 0; index < owner.methods().size(); ++index) {
    const std::string name(owner.constant_pool().get_utf8(owner.methods()[index].name_index()).value());
    auto graph = checked(lift_ssa(owner, index, context), name);
    // Require each method to follow the graph-editing path, including those used in the differential trace.
    insert_nop(graph);
    if (index == editable)
      change_return(graph);
    install(owner, graph, context, name);
  }
}

void rewrite_fixture(const path& original, const path& rewritten, AnalysisContext& context) {
  auto owner = parse(read_bytes(original));
  reconstruct(owner, context, owner.find_method("editable", "()I").value());
  authored_method(owner, context);
  const auto emitted = checked(write_class(owner), "ordinary write after SSA lowering");
  require(emitted.diagnostics.empty(), "unexpected writer diagnostics");
  save(rewritten, emitted.bytes);
  auto reparsed = parse(emitted.bytes);
  require(reparsed.find_method("authored", "()I").has_value(), "authored method missing after serialization");
  check_catch_annotation(reparsed);
}

void rewrite_methods(const path& original, const path& rewritten, AnalysisContext& context) {
  auto owner = parse(read_bytes(original));
  reconstruct(owner, context);
  save(rewritten, checked(write_class(owner), original.filename().string()).bytes);
}

std::vector<std::byte> dead_fixture(bool seed) {
  auto owner = ClassFile::create({52, 0}, "SsaDead").value();
  auto& pool = owner.constant_pool();
  const auto index = declare_method(owner, "answer");
  auto& attribute = owner.add_code(index, pool.append_utf8("Code").value()).value().get();
  auto& code = attribute.code();
  code.append(Instruction::simple(Opcode::IConst4).value()).value();
  code.append(Instruction::simple(Opcode::IReturn).value()).value();
  const auto first = code.append(Instruction::simple(Opcode::Pop2).value()).value();
  code.append(Instruction::iload(0)).value();
  code.append(Instruction::istore(0)).value();
  code.append(Instruction::simple(Opcode::LConst1).value()).value();
  const auto dead = code.make_label().value();
  code.append(Instruction::branch(Opcode::Goto, dead).value()).value();
  code.bind_label(dead, CodePosition{first}).value();
  std::vector<Attribute> nested;
  if (seed) {
    // At byte offset two, the disconnected loop starts with int in local zero and a long on the stack.
    const std::array<std::uint8_t, 11> table{0, 1, 255, 0, 2, 0, 1, 1, 0, 1, 4};
    const auto body = std::as_bytes(std::span(table));
    nested.push_back(Attribute::raw(pool.append_utf8("StackMapTable").value(), {body.begin(), body.end()}));
  }
  attribute.supply_metadata(2, 1, {}, std::move(nested));
  auto written = checked(write_class(owner, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata}), "generate dead fixture");
  return std::move(written.bytes);
}

std::vector<std::byte> dynamic_fixture(AnalysisContext& context) {
  auto generated = ClassFile::create({55, 0}, "SsaDynamic").value();
  auto& pool = generated.constant_pool();
  const auto code_name = pool.append_utf8("Code").value();
  const auto constants = declare_method(generated, "constants");
  const auto calls = declare_method(generated, "calls");
  auto& loads = generated.add_code(constants, code_name).value().get().code();
  auto& sites = generated.add_code(calls, code_name).value().get().code();
  const Bootstrap constant{{MethodHandleKind::InvokeStatic, "VerifySsa", "constant",
                            "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;)I"},
                           {}};
  const auto first = generated.edit_method(constants)->append(DynamicConstant{"n", "I", constant}).value();
  const auto index = loads.get(first)->constant_index().value();
  // Byte-identical CONSTANT_Dynamic entries resolve separately; repeated references to one entry share its value.
  const auto duplicate = pool.append(*pool.get(index)).value();
  loads.append(Instruction::constant(Opcode::Ldc, duplicate).value()).value();
  loads.append(Instruction::simple(Opcode::IAdd).value()).value();
  loads.append(Instruction::constant(Opcode::Ldc, index).value()).value();
  loads.append(Instruction::simple(Opcode::IAdd).value()).value();
  loads.append(Instruction::simple(Opcode::IReturn).value()).value();

  const Bootstrap invocation{{MethodHandleKind::InvokeStatic, "VerifySsa", "invocation",
                              "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;)"
                              "Ljava/lang/invoke/CallSite;"},
                             {}};
  const auto first_site = generated.edit_method(calls)->append(DynamicInvocation{"f", "()I", invocation}).value();
  // InvokeDynamic resolves per instruction, including two instructions that reference the very same pool entry.
  sites.append(sites.get(first_site).value()).value();
  sites.append(Instruction::simple(Opcode::IAdd).value()).value();
  sites.append(Instruction::simple(Opcode::IReturn).value()).value();
  return checked(write_class(generated, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context),
                 "generate dynamic fixture")
      .bytes;
}

void test_irreducible(const path& original, const path& rewritten, AnalysisContext& context) {
  auto generated = ClassFile::create({61, 0}, "SsaTwoEntryLoop").value();
  const auto index = declare_method(generated, "walk", "(ZI)I");
  auto& code = generated.add_code(index, generated.constant_pool().append_utf8("Code").value()).value().get().code();
  const auto left = code.make_label().value();
  const auto right = code.make_label().value();
  const auto exit = code.make_label().value();
  code.append(Instruction::simple(Opcode::IConst0).value()).value();
  code.append(Instruction::istore(2)).value();
  code.append(Instruction::iload(0)).value();
  code.append(Branch{Opcode::IfNe, right}).value();
  // Both halves of the cycle are reachable directly from method entry.
  const auto first = code.append(Instruction::increment(2, 3)).value();
  code.append(Instruction::increment(1, -1)).value();
  code.append(Instruction::iload(1)).value();
  code.append(Branch{Opcode::IfLe, exit}).value();
  const auto second = code.append(Instruction::increment(2, 5)).value();
  code.append(Instruction::increment(1, -1)).value();
  code.append(Instruction::iload(1)).value();
  code.append(Branch{Opcode::IfGt, left}).value();
  const auto done = code.append(Instruction::iload(2)).value();
  code.append(Instruction::simple(Opcode::IReturn).value()).value();
  code.bind_label(left, {first}).value();
  code.bind_label(right, {second}).value();
  code.bind_label(exit, {done}).value();
  const auto bytes = checked(write_class(generated, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context),
                             "generate irreducible loop");
  save(original, bytes.bytes);
  auto owner = parse(bytes.bytes);
  auto graph = checked(lift_ssa(owner, index, context), "lift irreducible loop");
  install(owner, graph, context, "lower irreducible loop");
  save(rewritten, checked(write_class(owner), "write irreducible loop").bytes);
}

void test_linkage(const path& original, const path& rewritten, AnalysisContext& context) {
  auto generated = ClassFile::create({61, 0}, "SsaLinkage").value();
  auto& pool = generated.constant_pool();
  const auto integer = pool.append_utf8("I").value();
  const auto field = pool.append_utf8("value").value();
  generated.add_field(0x0009, field, integer).value();
  const auto read = declare_method(generated, "read");
  const auto call = declare_method(generated, "call");
  const auto code_name = pool.append_utf8("Code").value();
  // Both members are static. The JVM verifies these instance access instructions and
  // raises IncompatibleClassChangeError before testing the null receiver at run time.
  const auto generate = [&](std::size_t index, Opcode opcode, CpTag tag, CpIndex name, CpIndex descriptor) {
    const auto pair = pool.append({CpTag::NameAndType, 0, name, descriptor}).value();
    const auto ref = pool.append({tag, 0, generated.this_class(), pair}).value();
    auto& body = generated.add_code(index, code_name).value().get();
    body.code().append(Instruction::simple(Opcode::AConstNull).value()).value();
    body.code().append(Instruction::constant(opcode, ref).value()).value();
    body.code().append(Instruction::simple(Opcode::IReturn).value()).value();
    body.supply_metadata(1, 0, {});
  };
  generate(read, Opcode::GetField, CpTag::Fieldref, field, integer);
  generate(call, Opcode::InvokeVirtual, CpTag::Methodref, generated.methods()[read].name_index(),
           generated.methods()[read].descriptor_index());
  const auto bytes = checked(write_class(generated, {.code_policy = CodeWritePolicy::CallerSuppliedMetadata}), "generate linkage fixture").bytes;
  save(original, bytes);
  auto owner = parse(bytes);
  for (const auto method : {read, call}) {
    auto graph = checked(lift_ssa(owner, method, context), "lift delayed linkage error");
    install(owner, graph, context, "lower delayed linkage error");
  }
  save(rewritten, checked(write_class(owner), "write linkage fixture").bytes);
}

void test_dynamic(const path& original, const path& rewritten, AnalysisContext& context) {
  const auto bytes = dynamic_fixture(context);
  save(original, bytes);
  auto owner = parse(bytes);
  for (std::size_t method = 0; method < owner.methods().size(); ++method) {
    auto graph = checked(lift_ssa(owner, method, context), "lift dynamic references");
    insert_nop(graph);
    install(owner, graph, context, "lower dynamic references");
  }
  save(rewritten, checked(write_class(owner), "write dynamic fixture").bytes);
}

void test_dead(const path& original, const path& rewritten, AnalysisContext& context) {
  auto missing_frame = parse(dead_fixture(false));
  const auto rejected = lift_ssa(missing_frame, 0, context);
  require(!rejected, "lifting a disconnected loop without a frame must fail");
  const auto bytes = dead_fixture(true);
  save(original, bytes);
  auto owner = parse(bytes);
  auto graph = checked(lift_ssa(owner, 0, context), "lift dead root");
  bool found_dead_root = false;
  for (const auto& block : graph.blocks())
    for (const auto edge : block.incoming)
      found_dead_root |= graph.edge(edge)->kind == SsaEdgeKind::DeadRoot;
  require(found_dead_root, "disconnected loop has no verifier root");
  insert_nop(graph);
  install(owner, graph, context, "lower dead root");
  const auto emitted = checked(write_class(owner), "write disconnected loop");
  save(rewritten, emitted.bytes);
  auto output = parse(emitted.bytes);
  const auto& code = output.methods().front().code()->code();
  require(std::ranges::any_of(code.instructions(), [](const InstructionEntry& entry) {
            return entry.instruction.opcode() == Opcode::LConst1;
          }),
          "lowering discarded the disconnected loop body");
}

} // namespace

int main(int argc, char** argv) try {
  require(argc >= 5, "usage: rewrite_ssa original-dir output-dir runtime-mode runtime-path [fixture ...]");
  const path original = argv[1];
  const path rewritten = argv[2];
  classpath::ClassPathBuilder builder;
  const std::string_view mode = argv[3];
  checked(mode == "--java-home" ? builder.add_java_home(argv[4]) : builder.add_directory(argv[4]), "register runtime");
  checked(builder.add_directory(original), "register fixtures");
  AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  test_dead(original / "SsaDead.class", rewritten / "SsaDead.class", context);
  test_irreducible(original / "SsaTwoEntryLoop.class", rewritten / "SsaTwoEntryLoop.class", context);
  test_dynamic(original / "SsaDynamic.class", rewritten / "SsaDynamic.class", context);
  test_linkage(original / "SsaLinkage.class", rewritten / "SsaLinkage.class", context);
  rewrite_fixture(original / "SsaFixture.class", rewritten / "SsaFixture.class", context);
  rewrite_methods(original / "AnalysisFixture.class", rewritten / "AnalysisFixture.class", context);
  for (int i = 5; i < argc; ++i) {
    const auto file = std::string(argv[i]) + ".class";
    rewrite_methods(original / file, rewritten / file, context);
  }
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
