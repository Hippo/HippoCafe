#pragma once

#include <cstddef>
#include <string_view>

namespace cafe::fuzz {

inline constexpr std::size_t MAX_INPUT_BYTES = 512 * 1024;

void exercise_reader(std::string_view input);
void exercise_roundtrip(std::string_view input);

} // namespace cafe::fuzz
