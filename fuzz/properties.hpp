#pragma once

#include <cstddef>
#include <string_view>

namespace cafe::fuzz {

inline constexpr std::size_t MAX_INPUT_BYTES = 512 * 1024;
inline constexpr std::size_t MAX_PROGRAM_BYTES = 12;

void exercise_reader(std::string_view input);
void exercise_roundtrip(std::string_view input);
void exercise_analysis(std::string_view input);
void exercise_ssa(std::string_view input);
void exercise_ssa_program(std::string_view input);
void exercise_archive(std::string_view input);

} // namespace cafe::fuzz
