#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include <hippo/cafe.hpp>

namespace {

using Clock = std::chrono::steady_clock;

class AllocationCounts final : public std::pmr::memory_resource {
public:
  std::size_t calls = 0;
  std::size_t live = 0;
  std::size_t peak = 0;

private:
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    auto* pointer = std::pmr::new_delete_resource()->allocate(size, alignment);
    ++calls;
    live += size;
    peak = std::max(live, peak);
    return pointer;
  }

  void do_deallocate(void* pointer, std::size_t size, std::size_t alignment) override {
    live -= size;
    std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
  }

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

struct Input {
  std::filesystem::path path;
  std::vector<std::byte> bytes;
};

std::vector<Input> load_inputs(const std::filesystem::path& path) {
  std::vector<std::filesystem::path> paths;
  if (std::filesystem::is_regular_file(path)) {
    paths.push_back(path);
  } else {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file() && entry.path().extension() == ".class") {
        paths.push_back(entry.path());
      }
    }
    std::ranges::sort(paths);
  }

  std::vector<Input> inputs;
  inputs.reserve(paths.size());
  for (const auto& input_path : paths) {
    const auto size = std::filesystem::file_size(input_path);
    if (size > cafe::ReadLimits{}.max_input_bytes) {
      throw std::runtime_error("Benchmark input exceeds the default read limit: " + input_path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream file(input_path, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
      throw std::runtime_error("Cannot read benchmark input " + input_path.string());
    }
    inputs.push_back({input_path, std::move(bytes)});
  }
  return inputs;
}

double seconds(Clock::duration elapsed) {
  return std::chrono::duration<double>(elapsed).count();
}

void benchmark_roundtrips(const std::vector<Input>& corpus, std::size_t rounds) {
  AllocationCounts allocations;
  const cafe::ReadOptions options{.mode = cafe::ParseMode::Strict, .upstream_resource = &allocations};
  Clock::duration parse_time{};
  Clock::duration write_time{};
  std::size_t total_size = 0;
  std::size_t retained_bytes = 0;
  std::size_t instruction_count = 0;
  for (std::size_t round = 0; round < rounds; ++round) {
    for (const auto& input : corpus) {
      const auto parse_start = Clock::now();
      const auto parsed = cafe::read_class(input.bytes, options);
      parse_time += Clock::now() - parse_start;
      if (!parsed) {
        throw std::runtime_error(input.path.string() + " " + parsed.error().path + ": " + parsed.error().detail);
      }

      retained_bytes += allocations.live + parsed->class_file.source_bytes().size();
      for (const auto& method : parsed->class_file.methods()) {
        if (const auto* code = method.code()) {
          instruction_count += code->code().size();
        }
      }

      const auto write_start = Clock::now();
      const auto written = cafe::write_class(parsed->class_file);
      write_time += Clock::now() - write_start;
      if (!written || written->bytes != input.bytes) {
        throw std::runtime_error("Round trip did not reproduce " + input.path.string());
      }
      total_size += input.bytes.size();
    }
  }

  constexpr double MIB = 1024 * 1024;
  const auto classes = corpus.size() * rounds;
  const auto input_mib = static_cast<double>(total_size) / MIB;
  std::cout << classes << " classes (" << corpus.size() << " distinct), " << instruction_count << " instructions\n"
            << "Parse " << input_mib / seconds(parse_time) << " MiB/s\n"
            << "Write " << input_mib / seconds(write_time) << " MiB/s\n"
            << "Parse upstream PMR allocations " << allocations.calls << " total, "
            << static_cast<double>(allocations.calls) / classes << " per class\n"
            << "Peak live upstream PMR bytes " << allocations.peak << "\n"
            << "Mean retained bytes per class (PMR plus source) " << static_cast<double>(retained_bytes) / classes << '\n';
}

void benchmark_edits(std::size_t instructions) {
  AllocationCounts allocations;
  cafe::MethodCode code(&allocations);
  const auto initial_bytes = allocations.live;
  code.reserve(instructions + 1);
  const auto nop = cafe::Instruction::simple(cafe::Opcode::Nop).value();
  for (std::size_t i = 0; i < instructions; ++i) {
    if (!code.append(nop)) {
      throw std::runtime_error("Could not populate instruction edit benchmark");
    }
  }
  const auto retained = static_cast<double>(allocations.live - initial_bytes) / instructions;
  const auto midpoint = code.at(instructions / 2);
  constexpr std::size_t EDITS = 10'000;
  const auto start = Clock::now();
  for (std::size_t i = 0; i < EDITS; ++i) {
    const auto inserted = code.insert_before(midpoint, nop);
    if (!inserted || !code.erase(*inserted)) {
      throw std::runtime_error("Could not edit benchmark method");
    }
  }
  const auto elapsed = Clock::now() - start;
  std::cout << instructions << " instructions, " << retained << " retained slot/order bytes per instruction, "
            << EDITS / seconds(elapsed) << " midpoint insert/erase pairs/s\n";
}

} // namespace

int main(int argc, char** argv) try {
  if (argc > 3) {
    throw std::runtime_error("Usage: hippocafe_benchmarks [class-file-or-directory] [rounds]");
  }
  std::size_t rounds = 10;
  if (argc == 3) {
    const std::string_view number(argv[2]);
    const auto result = std::from_chars(number.data(), number.data() + number.size(), rounds);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || rounds == 0) {
      throw std::runtime_error("Round count must be a positive integer");
    }
  }
  const auto corpus = load_inputs(argc >= 2 ? argv[1] : HIPPOCAFE_FIXTURE_DIR);
  if (corpus.empty()) {
    throw std::runtime_error("No class files found in the input corpus");
  }

  std::cout << std::fixed << std::setprecision(2);
  benchmark_roundtrips(corpus, rounds);
  benchmark_edits(4096);
  benchmark_edits(32768);
#if defined(__linux__) || defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    constexpr long KIB = 1024;
#else
    constexpr long KIB = 1;
#endif
    std::cout << "Peak process RSS " << usage.ru_maxrss / KIB << " KiB\n";
  }
#endif
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
