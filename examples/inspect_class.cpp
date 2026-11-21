/**
 * @file
 * @brief Command-line inspection of class declarations and decoded methods.
 */

/** @cond example_programs */
#include <cstddef>
#include <expected>
#include <iostream>
#include <utility>

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"

// [scan-method]
std::expected<void, cafe::CodeEditError> scan_method(const cafe::ClassFile& file, std::size_t method_index) {
  using cafe::Opcode;
  for (auto insn : file.instructions(method_index)) {
    if (insn.is(Opcode::Bipush) && insn.immediate() == 10)
      std::cout << "  bipush 10\n";
    if (!insn.is_invoke())
      continue;
    auto call = insn.method_invocation();
    if (!call)
      return std::unexpected(call.error());
    if (insn.is(Opcode::InvokeStatic) && call->matches("java/lang/Math", "abs", "(I)I"))
      std::cout << "  invokestatic Math.abs(int)\n";
    if (insn.is(Opcode::InvokeVirtual) && call->matches("java/io/PrintStream", "println", "(Ljava/lang/String;)V"))
      std::cout << "  invokevirtual PrintStream.println(String)\n";
  }
  return {};
}
// [scan-method]

// [capture-prefix]
std::expected<void, cafe::pattern::MatchError> show_prefixes(const cafe::ClassFile& file, std::size_t method_index) {
  namespace p = cafe::pattern;
  auto pattern = p::seq(
      p::capture("prefix", p::repeat(p::any(), 1, 3)),
      p::invoke({.opcode = cafe::Opcode::InvokeStatic, .owner = "java/lang/Math", .name = "abs", .descriptor = "(I)I"}));
  auto found = p::find_all(file, method_index, pattern);
  if (!found)
    return std::unexpected(found.error());
  for (const auto& match : *found) {
    std::cout << "  Math.abs prefix at instruction " << match.start_index() << '\n';
    if (auto prefix = match.capture("prefix")) {
      for (auto insn : prefix->instructions())
        std::cout << "    " << cafe::format(insn) << '\n';
    }
  }
  return {};
}
// [capture-prefix]

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: hippocafe_inspect_class input.class\n";
    return 1;
  }

  auto input = examples::load_class(argv[1]);
  if (!input) {
    std::cerr << input.error() << '\n';
    return 1;
  }
  const auto parsed = cafe::read_class(std::move(*input), {.allow_opaque_code = true});
  if (!parsed) {
    std::cerr << parsed.error().path << " at byte " << parsed.error().offset << ": " << parsed.error().detail << '\n';
    return 1;
  }
  const auto& file = parsed->class_file;
  const auto& pool = file.constant_pool();
  const auto version = file.version();
  const auto* identity = pool.get(file.this_class());
  std::cout << *pool.get_utf8(identity->first) << " (" << version.major << '.' << version.minor << ")\n";
  std::cout << pool.size() - 1 << " constant pool slots, " << file.fields().size() << " fields, "
            << file.methods().size() << " methods\n";
  for (std::size_t i = 0; i < file.methods().size(); ++i) {
    const auto& method = file.methods()[i];
    std::cout << *pool.get_utf8(method.name_index()) << *pool.get_utf8(method.descriptor_index());
    if (const auto* code = method.code())
      std::cout << "  " << code->code().size() << " instructions";
    if (method.opaque_code())
      std::cout << "  opaque Code";
    std::cout << '\n';
    if (method.code()) {
      if (auto scan = scan_method(file, i); !scan) {
        std::cerr << "methods[" << i << "]: instruction inspection failed (" << static_cast<int>(scan.error()) << ")\n";
        return 1;
      }
      if (auto prefixes = show_prefixes(file, i); !prefixes) {
        std::cerr << "methods[" << i << "]: " << prefixes.error().detail << '\n';
        return 1;
      }
    }
  }
  for (const auto& warning : parsed->diagnostics)
    std::cerr << warning.path << " at byte " << warning.offset << ": " << warning.detail << '\n';
}

/** @endcond */
