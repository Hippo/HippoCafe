#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <hippo/cafe.hpp>

#include "io/binary_reader.hpp"

namespace {

using namespace cafe;
using cafe::detail::BinaryReader;
using std::filesystem::path;

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

std::vector<std::byte> load(const path& file) {
  std::vector<std::byte> bytes(std::filesystem::file_size(file));
  std::ifstream input(file, std::ios::binary);
  require(bool(input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))),
          "could not read fixture");
  return bytes;
}

ClassFile parse(std::span<const std::byte> bytes) {
  auto parsed = read_class(bytes, {.mode = ParseMode::Strict});
  if (!parsed)
    throw std::runtime_error(parsed.error().path + " " + parsed.error().detail);
  return std::move(parsed->class_file);
}

std::span<const std::byte> attribute(const ClassFile& owner, const CodeAttribute& code, std::string_view name) {
  for (const auto& entry : code.attributes()) {
    const auto text = owner.constant_pool().get_utf8(entry.name_index());
    if (text && *text == name)
      return entry.get<RawAttribute>()->bytes(owner).value();
  }
  throw std::runtime_error("missing Code attribute " + std::string(name));
}

const CodeAttribute& method_code(const ClassFile& owner, std::string_view name, std::string_view descriptor) {
  return *owner.methods()[owner.find_method(name, descriptor).value()].code();
}

struct WireFrames {
  std::vector<unsigned> offsets;
  std::vector<unsigned> allocation_offsets;
  std::vector<CpIndex> reference_indices;
};

// Inspect the actual wire deltas and verification tags independently of the
// writer's logical instruction-to-offset maps.
WireFrames read_frames(const ClassFile& owner, const CodeAttribute& code) {
  BinaryReader reader(attribute(owner, code, "StackMapTable"));
  WireFrames frames;
  const auto count = reader.u2().value();
  int previous = -1;
  for (unsigned i = 0; i < count; ++i) {
    require(reader.u1() == 255, "expected full_frame encoding");
    previous += reader.u2().value() + 1;
    frames.offsets.push_back(static_cast<unsigned>(previous));
    for (unsigned area = 0; area < 2; ++area) {
      const auto entries = reader.u2().value();
      for (unsigned entry = 0; entry < entries; ++entry) {
        const auto kind = reader.u1().value();
        require(kind <= 8, "invalid verification_type_info");
        if (kind == 7)
          frames.reference_indices.push_back(CpIndex{reader.u2().value()});
        if (kind == 8)
          frames.allocation_offsets.push_back(reader.u2().value());
      }
    }
  }
  require(reader.remaining() == 0, "trailing StackMapTable bytes");
  return frames;
}

void write(const ClassFile& owner, analysis::AnalysisContext& context, const path& output_path) {
  const auto result = write_class(owner, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context);
  if (!result)
    throw std::runtime_error(result.error().path + " " + result.error().detail);
  require(result->diagnostics.empty(), "unexpected writer diagnostic");
  std::ofstream output(output_path, std::ios::binary);
  require(bool(output.write(reinterpret_cast<const char*>(result->bytes.data()),
                            static_cast<std::streamsize>(result->bytes.size()))),
          "could not write fixture");
}

void rewrite_compiled(const path& fixture_dir, const path& output_dir, analysis::AnalysisContext& context) {
  auto owner = parse(load(fixture_dir / "AnalysisFixture.class"));
  for (std::size_t i = 0; i < owner.methods().size(); ++i) {
    auto* method = owner.edit_code(i);
    if (method)
      require(method->code().insert_before(method->code().at(0), Instruction::nop()).has_value(), "could not shift method");
  }
  auto* handlers = owner.edit_code(owner.find_method("handlers", "(I)I").value());
  require(handlers->exception_handlers().size() == 2, "expected two compiled catch entries");
  const auto reversed = std::array{handlers->exception_handlers()[1], handlers->exception_handlers()[0]};
  handlers->set_exception_handlers(reversed);
  owner.constant_pool().append_utf8("unused before reclamation").value();
  const auto compacted = owner.compact_constant_pool();
  if (!compacted)
    throw std::runtime_error(compacted.error().path + " " + compacted.error().detail);
  require(*compacted != 0, "expected reclaimed pool slots");
  const auto file = output_dir / "AnalysisFixture.class";
  write(owner, context, file);
  const auto rewritten = parse(load(file));

  const auto& caught = method_code(rewritten, "handlers", "(I)I");
  BinaryReader catch_target(attribute(rewritten, caught, "RuntimeVisibleTypeAnnotations"));
  require(catch_target.u2() == 1 && catch_target.u1() == 0x42 && catch_target.u2() == 1,
          "catch annotation did not follow its reordered handler");
  const auto& allocation = method_code(rewritten, "annotatedNew", "()Ljava/lang/Object;");
  BinaryReader new_target(attribute(rewritten, allocation, "RuntimeVisibleTypeAnnotations"));
  require(new_target.u2() == 1 && new_target.u1() == 0x44 && new_target.u2() == 1,
          "type annotation did not follow its shifted new instruction");
  BinaryReader line(attribute(rewritten, allocation, "LineNumberTable"));
  require(line.u2() == 1 && line.u2() == 1, "line table did not follow its shifted instruction");
  BinaryReader locals(attribute(rewritten, caught, "LocalVariableTable"));
  const auto local_count = locals.u2();
  require(local_count && *local_count > 0, "local variable ranges were lost");
}

MethodCode& add_method(ClassFile& owner, std::string_view name, std::string_view descriptor, CpIndex code) {
  auto& pool = owner.constant_pool();
  const auto index = owner.add_method(0x0009, pool.append_utf8(name).value(), pool.append_utf8(descriptor).value()).value();
  return owner.add_code(index, code).value().get().code();
}

Instruction simple(Opcode opcode) {
  return Instruction::simple(opcode).value();
}

Branch branch(Opcode opcode, LabelId target) {
  return Instruction::branch(opcode, target).value();
}

void generate(const path& output_dir, analysis::AnalysisContext& context) {
  auto owner = ClassFile::create({52, 0}, "GeneratedFrames").value();
  auto& pool = owner.constant_pool();
  const auto code_name = pool.append_utf8("Code").value();
  auto& wide = add_method(owner, "wideBranch", "(Z)I", code_name);
  const auto far = wide.make_label().value();
  wide.append(Instruction::iload(0)).value();
  wide.append(branch(Opcode::IfEq, far)).value();
  const std::vector<InstructionBuilder> padding(33'000, Instruction::nop());
  wide.append(padding).value();
  wide.append(simple(Opcode::IConst1)).value();
  wide.append(simple(Opcode::IReturn)).value();
  const auto far_instruction = wide.append(simple(Opcode::IConst2)).value();
  wide.append(simple(Opcode::IReturn)).value();
  wide.bind_label(far, CodePosition{far_instruction}).value();

  auto& allocated = add_method(owner, "allocated", "(Z)Ljava/lang/StringBuilder;", code_name);
  const auto builder_name = pool.append_utf8("java/lang/StringBuilder").value();
  const auto builder_class = pool.append({CpTag::Class, 0, builder_name, {}}).value();
  const auto init = pool.append_utf8("<init>").value();
  const auto void_descriptor = pool.append_utf8("()V").value();
  const auto signature = pool.append({CpTag::NameAndType, 0, init, void_descriptor}).value();
  const auto constructor = pool.append({CpTag::Methodref, 0, builder_class, signature}).value();
  const auto join = allocated.make_label().value();
  allocated.append(Instruction::nop()).value();
  allocated.append(Instruction::constant(Opcode::New, builder_class).value()).value();
  allocated.append(Instruction::dup()).value();
  allocated.append(Instruction::iload(0)).value();
  allocated.append(branch(Opcode::IfEq, join)).value();
  allocated.append(Instruction::nop()).value();
  const auto call = allocated.append(Instruction::constant(Opcode::InvokeSpecial, constructor).value()).value();
  allocated.append(simple(Opcode::AReturn)).value();
  allocated.bind_label(join, CodePosition{call}).value();

  auto& array = add_method(owner, "array", "(Z)[I", code_name);
  const auto alternative = array.make_label().value();
  const auto array_exit = array.make_label().value();
  const auto int_array = Instruction::new_array(ArrayType::Int).value();
  array.append(Instruction::iload(0)).value();
  array.append(branch(Opcode::IfEq, alternative)).value();
  array.append(simple(Opcode::IConst1)).value();
  array.append(int_array).value();
  array.append(branch(Opcode::Goto, array_exit)).value();
  const auto second = array.append(simple(Opcode::IConst2)).value();
  array.append(int_array).value();
  const auto exit = array.append(simple(Opcode::AReturn)).value();
  array.bind_label(alternative, CodePosition{second}).value();
  array.bind_label(array_exit, CodePosition{exit}).value();

  auto& loop = add_method(owner, "loop", "(I)V", code_name);
  const auto top = loop.make_label().value();
  const auto done = loop.make_label().value();
  const auto first = loop.append(Instruction::iload(0)).value();
  loop.append(branch(Opcode::IfEq, done)).value();
  loop.append(Instruction::increment(0, -1)).value();
  loop.append(branch(Opcode::Goto, top)).value();
  const auto last = loop.append(Instruction::return_void()).value();
  loop.bind_label(top, CodePosition{first}).value();
  loop.bind_label(done, CodePosition{last}).value();

  for (unsigned i = 0; i < 256; ++i)
    pool.append_utf8("unused").value();
  const auto number = pool.append({CpTag::Integer, 0, {}, {}, 1234}).value();
  auto& load_constant = add_method(owner, "wideConstant", "()I", code_name);
  load_constant.append(Instruction::constant(Opcode::Ldc, number).value()).value();
  load_constant.append(simple(Opcode::IReturn)).value();

  auto& init_code = add_method(owner, "initializeWithHandler", "()Ljava/lang/Object;", code_name);
  init_code.append(Instruction::constant(Opcode::New, builder_class).value()).value();
  init_code.append(Instruction::dup()).value();
  init_code.append(Instruction::astore(0)).value();
  const auto init_call = init_code.append(Instruction::constant(Opcode::InvokeSpecial, constructor).value()).value();
  const auto init_exit = init_code.append(Instruction::aload(0)).value();
  init_code.append(simple(Opcode::AReturn)).value();
  const auto handler_entry = init_code.append(Instruction::pop()).value();
  init_code.append(simple(Opcode::AConstNull)).value();
  init_code.append(simple(Opcode::AReturn)).value();
  const auto try_start = init_code.make_label().value();
  const auto try_end = init_code.make_label().value();
  const auto handler = init_code.make_label().value();
  init_code.bind_label(try_start, CodePosition{init_call}).value();
  init_code.bind_label(try_end, CodePosition{init_exit}).value();
  init_code.bind_label(handler, CodePosition{handler_entry}).value();
  const std::array handler_table{ExceptionHandler{try_start, try_end, handler, {}}};
  owner.edit_code(owner.find_method("initializeWithHandler", "()Ljava/lang/Object;").value())->set_exception_handlers(handler_table);

  const auto pool_size = pool.size();
  const auto file = output_dir / "GeneratedFrames.class";
  write(owner, context, file);
  require(pool.size() == pool_size, "writer mutated the original constant pool");
  const auto emitted = parse(load(file));
  const auto& long_branch = method_code(emitted, "wideBranch", "(Z)I");
  require(read_frames(emitted, long_branch).offsets == std::vector<unsigned>{9, 33'011},
          "expanded branch has incorrect wire frame deltas");
  const auto first_jump = long_branch.code().get(long_branch.code().at(1)).value();
  const auto wide_jump = long_branch.code().get(long_branch.code().at(2)).value();
  require(first_jump.opcode() == Opcode::IfNe && wide_jump.opcode() == Opcode::Goto &&
              wide_jump.encoding_hint() == EncodingHint::Wide,
          "conditional overflow did not expand to goto_w");
  const auto alloc_frames = read_frames(emitted, method_code(emitted, "allocated", "(Z)Ljava/lang/StringBuilder;"));
  require(alloc_frames.offsets == std::vector<unsigned>{10} &&
              alloc_frames.allocation_offsets == std::vector<unsigned>{1, 1},
          "allocation offsets did not follow new");
  const auto array_frames = read_frames(emitted, method_code(emitted, "array", "(Z)[I"));
  require(array_frames.reference_indices.size() == 1, "expected one Class reference in the array frame");
  const auto& out_pool = emitted.constant_pool();
  const auto* array_type = out_pool.get(array_frames.reference_indices[0]);
  require(array_type && array_type->tag == CpTag::Class && out_pool.get_utf8(array_type->first) == "[I",
          "array frame did not reference a valid CONSTANT_Class");
  require(read_frames(emitted, method_code(emitted, "loop", "(I)V")).offsets == std::vector<unsigned>{0, 10},
          "backedge to the entry requires an explicit frame at offset zero");
  const auto& widened_ldc = method_code(emitted, "wideConstant", "()I").code();
  require(widened_ldc.get(widened_ldc.at(0))->encoding_hint() == EncodingHint::Wide,
          "a large constant index must use ldc_w");
  const auto init_frames = read_frames(emitted, method_code(emitted, "initializeWithHandler", "()Ljava/lang/Object;"));
  require(init_frames.allocation_offsets.empty() && init_frames.reference_indices.size() == 1,
          "constructor handler must receive the catch type and no usable allocation alias");
}

void generate_symbolic(const path& output_dir, analysis::AnalysisContext& context) {
  auto owner = ClassFile::create({55, 0}, "SymbolicInstructions").value();
  const auto code_name = owner.constant_pool().append_utf8("Code").value();
  constexpr std::string_view CONCAT_DESCRIPTOR = "(I)Ljava/lang/String;";
  add_method(owner, "concat", CONCAT_DESCRIPTOR, code_name);
  auto editor = owner.edit_method(owner.find_method("concat", CONCAT_DESCRIPTOR).value()).value();
  editor.append(Instruction::iload(0)).value();
  editor.append(MethodInvocation{Opcode::InvokeStatic, "java/lang/Math", "abs", "(I)I"}).value();
  editor.append(FieldAccess{Opcode::GetStatic, "java/lang/Integer", "MAX_VALUE", "I"}).value();
  const std::array arguments{
      BootstrapArgument{std::string_view("\x01:\x01:\x02")},
      BootstrapArgument{std::string_view("checked")},
  };
  const Bootstrap concat{{
                             MethodHandleKind::InvokeStatic,
                             "java/lang/invoke/StringConcatFactory",
                             "makeConcatWithConstants",
                             "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;"
                             "Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/invoke/CallSite;",
                         },
                         arguments};
  editor.append(DynamicInvocation{"concat", "(II)Ljava/lang/String;", concat}).value();
  editor.append(simple(Opcode::AReturn)).value();

  auto& wide_code = add_method(owner, "wide", "()J", code_name);
  wide_code.append(simple(Opcode::LConst0)).value();
  wide_code.append(simple(Opcode::LReturn)).value();
  const auto destination = output_dir / "SymbolicInstructions.class";
  write(owner, context, destination);

  auto parsed = parse(load(destination));
  auto wide = parsed.edit_method(parsed.find_method("wide", "()J").value()).value();
  const std::array value{BootstrapArgument{std::int64_t{1234567890123}}};
  const Bootstrap cast{{
                           MethodHandleKind::InvokeStatic,
                           "java/lang/invoke/ConstantBootstraps",
                           "explicitCast",
                           "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;"
                           "Ljava/lang/Object;)Ljava/lang/Object;",
                       },
                       value};
  wide.replace(wide.code()->code().at(0), DynamicConstant{"value", "J", cast}).value();
  write(parsed, context, destination);
}

void generate_branch_entries(const path& output_dir, analysis::AnalysisContext& context) {
  auto source = ClassFile::create({55, 0}, "BranchEntries").value();
  const auto code_name = source.constant_pool().append_utf8("Code").value();
  for (const auto name : {"preserve", "include", "ordered"}) {
    auto& code = add_method(source, name, "(I)I", code_name);
    const auto entry = code.make_label().value();
    const auto fallthrough = code.make_label().value();
    const auto handler = code.make_label().value();
    code.append(Instruction::iload(0)).value();
    code.append(branch(Opcode::IfEq, entry)).value();
    code.append(Instruction::iload(0)).value();
    const std::array cases{entry, fallthrough};
    code.append(TableSwitch{1, entry, cases}).value();
    const auto fallthrough_id = code.append(Instruction::nop()).value();
    const auto anchor = code.append(Instruction::iload(0)).value();
    code.append(simple(Opcode::IReturn)).value();
    const auto catch_entry = code.append(Instruction::pop()).value();
    code.append(simple(Opcode::IConstM1)).value();
    code.append(simple(Opcode::IReturn)).value();
    code.bind_label(entry, CodePosition{anchor}).value();
    code.bind_label(fallthrough, CodePosition{fallthrough_id}).value();
    code.bind_label(handler, CodePosition{catch_entry}).value();
    const std::array handlers{ExceptionHandler{entry, handler, handler, {}}};
    source.edit_code(source.find_method(name, "(I)I").value())->set_exception_handlers(handlers);
  }
  for (const auto name : {"preserveBounds", "includeBounds"}) {
    auto& code = add_method(source, name, "(I)I", code_name);
    const auto start = code.make_label().value();
    const auto fallthrough = code.make_label().value();
    const auto shared = code.make_label().value();
    // Slot 1 counts visits to the later insertion. Normal entry carries null
    // where exceptional entry carries the caught Throwable.
    const std::array<InstructionBuilder, 12> body{
        simple(Opcode::IConst0), Instruction::istore(1),
        simple(Opcode::AConstNull), Instruction::iload(0), branch(Opcode::IfEq, shared),
        Instruction::iload(0), branch(Opcode::IfGt, fallthrough), simple(Opcode::AThrow),
        Instruction::nop(), Instruction::pop(), Instruction::iload(1), simple(Opcode::IReturn)};
    const auto ids = code.append(body).value();
    code.bind_label(start, {ids[2]}).value();
    code.bind_label(fallthrough, {ids[8]}).value();
    code.bind_label(shared, {ids[9]}).value();
    const std::array handlers{ExceptionHandler{start, shared, shared, {}}};
    source.edit_code(source.find_method(name, "(I)I").value())->set_exception_handlers(handlers);
  }

  const auto wire = write_class(source, {.code_analysis = CodeAnalysisMode::ComputeFrames}, context).value();
  auto parsed = parse(wire.bytes);
  const auto returns_input = pattern::seq(pattern::iload(0), pattern::opcode(Opcode::IReturn));
  for (const std::string_view name : {"preserve", "include", "ordered"}) {
    auto editor = parsed.edit_method(name, "(I)I").value();
    const auto match = editor.find_first(returns_input).value().value();
    const auto& code = editor.code()->code();
    const auto entry = editor.code()->exception_handlers().front().start;
    const auto branch_id = code.at(1);
    const auto switch_id = code.at(3);
    const auto table = code.table_switch(code.switch_id(switch_id).value()).value();
    require(code.branch_target(branch_id) == entry && table.default_target == entry &&
                table.targets.front() == entry,
            "parsed fixture must share a label between branch, switch and exception range");
    const auto increment = [](CodeBuilder& builder) { builder.iinc(0, 5); };
    const auto patch = [&] {
      if (name == "include")
        return editor.insert_before_and_redirect_branches(match.start(), increment).value();
      if (name == "ordered")
        return editor.insert_before(CodeNode{match.start()}, increment).value();
      return editor.insert_before(match.start(), increment).value();
    }();
    const auto expected_entry = name == "preserve" ? match.start() : patch.front();
    const auto expected_try_start = name == "ordered" ? patch.front() : match.start();
    const auto target = [&](LabelId label) { return code.label_position(label).value().before; };
    const auto updated = code.table_switch(code.switch_id(switch_id).value()).value();
    require(target(code.branch_target(branch_id).value()) == expected_entry &&
                target(updated.default_target) == expected_entry && target(updated.targets.front()) == expected_entry &&
                target(editor.code()->exception_handlers().front().start) == expected_try_start,
            "branch or exception range entry changed unexpectedly");
  }
  for (const auto name : {std::string_view{"preserveBounds"}, std::string_view{"includeBounds"}}) {
    auto editor = parsed.edit_method(parsed.find_method(name, "(I)I").value()).value();
    const auto& code = editor.code()->code();
    const auto handler = editor.code()->exception_handlers().front();
    const auto jump = code.at(4);
    require(code.branch_target(jump) == handler.handler && handler.end == handler.handler,
            "parsed fixture must share a branch target with end_pc and handler_pc");
    const auto anchor = code.label_position(handler.handler).value().before.value();
    const bool include = name == "includeBounds";
    const auto policy = include ? InsertionEntry::IncludeIncomingBranches : InsertionEntry::PreserveAnchor;
    const auto patch = editor.insert_before(anchor, [](auto& b) {
      const auto done = b.label();
      // Divide only on the first visit so an erroneous handler entry yields a
      // failing count instead of repeatedly throwing from the handler.
      b.iload(1).iinc(1, 1).ifne(done).iconst_1().iload(0).idiv().pop().bind(done); }, policy).value();
    require(code.label_position(code.branch_target(jump).value())->before == (include ? patch.front() : anchor) &&
                code.label_position(handler.end)->before == anchor && code.label_position(handler.handler)->before == anchor,
            "ordinary entry redirection changed the protected end or the handler target");
  }
  write(parsed, context, output_dir / "BranchEntries.class");
}

} // namespace

int main(int argc, char** argv) try {
  require(argc == 5, "usage: rewrite_for_jvm fixtures-or-dash output runtime-source-mode runtime-path");
  const path fixtures = argv[1];
  const path output = argv[2];
  const std::string_view mode = argv[3];
  classpath::ClassPathBuilder builder;
  const auto registered = mode == "--java-home" ? builder.add_java_home(argv[4]) : builder.add_directory(argv[4]);
  if (!registered)
    throw std::runtime_error("could not register runtime classes: " + registered.error().detail);
  if (fixtures != "-") {
    builder.add_directory(fixtures).value();
  }
  std::filesystem::create_directories(output);
  analysis::AnalysisContext context{classpath::ClassHierarchy{classpath::ClassResolver{builder.build()}}};
  generate(output, context);
  generate_symbolic(output, context);
  generate_branch_entries(output, context);
  if (fixtures != "-")
    rewrite_compiled(fixtures, output, context);
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
