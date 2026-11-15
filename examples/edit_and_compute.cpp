/**
 * @file
 * @brief Instruction editing and frame computation with an explicit runtime.
 */

/** @cond example_programs */
#include <array>
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

#include <hippo/cafe.hpp>

#include "class_file_io.hpp"

// [edit-method]
std::expected<void, cafe::CodeEditError> insert_nop(cafe::MethodCode& code, cafe::InstructionId anchor) {
  auto inserted = code.insert_before(anchor, cafe::Instruction::nop());
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
// [edit-method]

// [edit-block]
std::expected<void, cafe::BlockEditError> insert_push_pop(cafe::MethodCode& code, cafe::InstructionId anchor) {
  const std::array<cafe::InstructionBuilder, 2> patch{
      cafe::Instruction::bipush(42),
      cafe::Instruction::pop(),
  };
  auto inserted = code.insert_before(anchor, patch);
  if (!inserted)
    return std::unexpected(inserted.error());
  return {};
}
// [edit-block]

// [compute-frames]
std::expected<cafe::WriteResult, std::string> write_with_frames(
    const cafe::ClassFile& file, const std::filesystem::path& java_home, std::uint16_t target_release) {
  cafe::classpath::ClassPathBuilder builder;
  builder.set_target_release(target_release);
  if (auto added = builder.add_java_home(java_home); !added)
    return std::unexpected(std::move(added.error().detail));
  cafe::analysis::AnalysisContext context{
      cafe::classpath::ClassHierarchy{cafe::classpath::ClassResolver{builder.build()}},
  };
  auto written = cafe::write_class(
      file, {.code_analysis = cafe::CodeAnalysisMode::ComputeFrames}, context);
  if (!written)
    return std::unexpected(written.error().path + ": " + written.error().detail);
  return std::move(*written);
}
// [compute-frames]

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "Usage: hippocafe_edit_and_compute input.class output.class java-home target-release\n";
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
  for (std::size_t i = 0; i < parsed->class_file.methods().size(); ++i) {
    if (auto* code = parsed->class_file.edit_code(i); code && code->code().size() != 0) {
      const auto anchor = code->code().at(0);
      if (auto edited = insert_nop(code->code(), anchor); !edited) {
        std::cerr << "methods[" << i << "]: single-instruction edit failed (" << static_cast<int>(edited.error()) << ")\n";
        return 1;
      }
      if (auto edited = insert_push_pop(code->code(), anchor); !edited) {
        std::cerr << "methods[" << i << "]: block edit failed (" << static_cast<int>(edited.error().cause) << ")\n";
        return 1;
      }
    }
  }
  auto result = write_with_frames(parsed->class_file, argv[3], target_release);
  if (!result) {
    std::cerr << result.error() << '\n';
    return 1;
  }
  for (const auto& diagnostic : result->diagnostics)
    std::cerr << diagnostic.path << ": " << diagnostic.detail << '\n';
  if (auto saved = examples::save_class(argv[2], result->bytes); !saved) {
    std::cerr << argv[2] << ": " << saved.error() << '\n';
    return 1;
  }
  std::cout << result->bytes.size() << " bytes written after editing and frame computation\n";
}

/** @endcond */
