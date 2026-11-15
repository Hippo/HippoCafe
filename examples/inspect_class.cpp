#include <iostream>
#include <utility>

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"

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
  for (const auto& method : file.methods()) {
    std::cout << *pool.get_utf8(method.name_index()) << *pool.get_utf8(method.descriptor_index());
    if (const auto* code = method.code())
      std::cout << "  " << code->code().size() << " instructions";
    if (method.opaque_code())
      std::cout << "  opaque Code";
    std::cout << '\n';
  }
  for (const auto& warning : parsed->diagnostics)
    std::cerr << warning.path << " at byte " << warning.offset << ": " << warning.detail << '\n';
}
