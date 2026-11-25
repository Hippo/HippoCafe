/**
 * @file
 * @brief Instruction editing and frame computation with an explicit runtime.
 */

/** @cond example_programs */
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"

// [edit-block]
std::expected<void, cafe::BlockEditError> insert_abs(cafe::MethodEditor& editor, cafe::InstructionId anchor) {
  namespace i = cafe::insn;
  auto inserted = editor.insert_before(
      anchor, {i::bipush(10), i::invokestatic("java/lang/Math", "abs", "(I)I"), i::pop()});
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
// [edit-block]

// [edit-matches]
std::expected<void, std::string> measure_printed_strings(cafe::MethodEditor& editor) {
  namespace p = cafe::pattern;
  const p::InvokePattern println{
      .opcode = cafe::Opcode::InvokeVirtual,
      .owner = "java/io/PrintStream",
      .name = "println",
      .descriptor = "(Ljava/lang/String;)V",
  };
  auto calls = editor.find_all(p::invoke(println));
  if (!calls)
    return std::unexpected(std::string(calls.error().detail));

  for (const auto& match : *calls) {
    auto inserted = editor.insert_before_and_redirect_branches(
        match.start(),
        [](cafe::CodeBuilder& code) {
          const auto skip = code.label();
          code.dup();
          code.ifnull(skip);
          code.dup();
          code.invokevirtual("java/lang/String", "length", "()I");
          code.pop();
          code.bind(skip);
        });
    if (!inserted)
      return std::unexpected("insertion failed at instruction " + std::to_string(match.start_index()) +
                             " (edit error " + std::to_string(static_cast<int>(inserted.error().cause)) + ")");
  }
  return {};
}
// [edit-matches]

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "Usage: hippocafe_edit_and_compute input.class output.class java-home target-release [dependency ...]\n";
    return 1;
  }
  const std::string_view release_text(argv[4]);
  std::uint16_t target_release = 0;
  const auto [end, error] = std::from_chars(release_text.data(), release_text.data() + release_text.size(), target_release);
  if (error != std::errc{} || end != release_text.data() + release_text.size() || target_release == 0) {
    std::cerr << "target-release must be a positive Java release number matching the supplied runtime\n";
    return 1;
  }
  auto bytes = examples::load_class(argv[1]);
  if (!bytes) {
    std::cerr << argv[1] << ": " << bytes.error() << '\n';
    return 1;
  }
  auto parsed = cafe::read_class(std::move(*bytes));
  if (!parsed) {
    std::cerr << parsed.error().path << ": " << parsed.error().detail << '\n';
    return 1;
  }
  for (const auto& diagnostic : parsed->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  auto& file = parsed->class_file;
  for (std::size_t i = 0; i < file.methods().size(); ++i) {
    auto editor = file.edit_method(i);
    if (!editor) {
      std::cerr << "methods[" << i << "]: could not open method editor\n";
      return 1;
    }
    const auto instructions = editor->instructions();
    if (instructions.empty())
      continue;
    const auto anchor = (*instructions.begin()).id();
    if (auto edited = insert_abs(*editor, anchor); !edited) {
      std::cerr << "methods[" << i << "]: block edit failed (" << static_cast<int>(edited.error().cause) << ")\n";
      return 1;
    }
    if (auto edited = measure_printed_strings(*editor); !edited) {
      std::cerr << "methods[" << i << "]: " << edited.error() << '\n';
      return 1;
    }
  }
  const std::vector<std::filesystem::path> dependencies(argv + 5, argv + argc);
  // [compute-frames]
  auto result = cafe::write_class_with_frames(file, argv[3], target_release, dependencies);
  if (!result) {
    std::cerr << result.error().path << ": " << result.error().detail << '\n';
    return 1;
  }
  // [compute-frames]
  for (const auto& diagnostic : result->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  if (auto saved = examples::save_class(argv[2], result->bytes); !saved) {
    std::cerr << argv[2] << ": " << saved.error() << '\n';
    return 1;
  }
  std::cout << result->bytes.size() << " bytes written after editing and frame computation\n";
}

/** @endcond */
