#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <stdexcept>
#include <string>
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

std::size_t add_method(cafe::ClassFile& owner, std::string_view name, std::string_view descriptor, cafe::CpIndex code_name) {
  auto& pool = owner.constant_pool();
  const auto index = owner.add_method(0x0009, pool.append_utf8(name).value(), pool.append_utf8(descriptor).value()).value();
  owner.add_code(index, code_name).value();
  return index;
}

cafe::ClassFile analysis_workload(std::size_t tiny_methods, std::size_t switch_count) {
  using namespace cafe;
  auto owner = ClassFile::create({65, 0}, "AnalysisBenchmark").value();
  auto& pool = owner.constant_pool();
  const auto code_name = pool.append_utf8("Code").value();
  for (std::size_t i = 0; i < tiny_methods; ++i) {
    const auto method = add_method(owner, "sum" + std::to_string(i), "(JJ)J", code_name);
    const std::array<InstructionBuilder, 4> body{
        Instruction::lload(0), Instruction::lload(2), Instruction::simple(Opcode::LAdd).value(),
        Instruction::simple(Opcode::LReturn).value()};
    owner.edit_code(method)->code().append(body).value();
  }
  const auto method = add_method(owner, "switches", "(I)V", code_name);
  auto& code = owner.edit_code(method)->code();
  auto boundary = code.make_label().value();
  for (std::size_t index = 0; index < switch_count; ++index) {
    const auto load = code.append(Instruction::iload(0)).value();
    code.bind_label(boundary, CodePosition{load}).value();
    boundary = code.make_label().value();
    const std::array cases{LookupPair{1, boundary}, LookupPair{5, boundary}, LookupPair{100, boundary}};
    code.append(LookupSwitch{boundary, cases}).value();
  }
  const auto exit = code.append(Instruction::return_void()).value();
  code.bind_label(boundary, CodePosition{exit}).value();
  return owner;
}

void benchmark_analysis(std::size_t tiny_methods, std::size_t switches, std::size_t rounds) {
  using namespace cafe;
  const auto workload = analysis_workload(tiny_methods, switches);
  AllocationCounts allocations;
  analysis::AnalysisContext context{analysis::AnalysisLimits{.upstream_resource = &allocations}};
  const auto measure = [&](bool frames) {
    allocations.calls = 0;
    allocations.peak = 0;
    std::size_t stack = 0;
    const auto start = Clock::now();
    for (std::size_t round = 0; round < rounds; ++round) {
      for (const auto& method : workload.methods()) {
        stack += frames ? analysis::compute_frames(workload, method, context).value().max_stack()
                        : analysis::compute_max_stack(workload, method, context.limits()).value();
      }
    }
    const auto elapsed = seconds(Clock::now() - start) * 1'000;
    std::cout << (frames ? "Frames " : "Stack shapes ") << elapsed << " ms, " << allocations.calls
              << " upstream allocations, " << allocations.peak << " peak live upstream bytes\n";
    return stack;
  };
  std::cout << tiny_methods << " tiny methods and " << switches << " switches, " << rounds << " rounds\n";
  const auto max_stack = measure(false);
  if (measure(true) != max_stack)
    throw std::runtime_error("Computed maxima disagree with frame analysis");

  const WriteOptions computed{.code_analysis = CodeAnalysisMode::ComputeFrames};
  const auto prepared = write_class(workload, computed, context).value();
  const auto parsed = read_class(prepared.bytes).value();
  for (const auto options : {WriteOptions{}, computed}) {
    const auto start = Clock::now();
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < rounds; ++i)
      bytes += write_class(parsed.class_file, options, context).value().bytes.size();
    const auto elapsed = seconds(Clock::now() - start) * 1'000;
    std::cout << (options.code_analysis == CodeAnalysisMode::Preserve ? "Preserve write " : "ComputeFrames write ")
              << elapsed << " ms, " << bytes << " bytes\n";
  }
}

void benchmark_hierarchy(const std::filesystem::path& java_home) {
  using namespace cafe::classpath;
  ClassPathBuilder builder;
  const auto start = Clock::now();
  const auto added = builder.add_java_home(java_home);
  if (!added)
    throw std::runtime_error(added.error().detail);
  const ClassResolver resolver(builder.build());
  const auto indexing = seconds(Clock::now() - start) * 1'000;
  const ClassHierarchy hierarchy(resolver);
  constexpr std::array CHECKS{
      std::pair{"java/util/ArrayList", "java/util/List"},
      std::pair{"java/util/HashMap", "java/util/Map"},
      std::pair{"java/lang/String", "java/io/Serializable"},
  };
  const auto time_queries = [&](std::size_t iterations) {
    const auto before = resolver.stats();
    const auto query_start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
      for (const auto& [type, parent] : CHECKS) {
        if (!resolver.resolve(type) || !hierarchy.is_assignable(type, parent).value())
          throw std::runtime_error("Unexpected standard hierarchy");
      }
    }
    const auto elapsed = seconds(Clock::now() - query_start) * 1'000;
    const auto after = resolver.stats();
    const auto hits = after.cache_hits - before.cache_hits;
    const auto scans = after.metadata_scans - before.metadata_scans;
    std::cout << elapsed << " ms, " << scans << " metadata scans, "
              << after.archive_entries_inflated - before.archive_entries_inflated << " class inflations, "
              << hits << " cached lookups\n";
  };
  std::cout << "Java home indexing " << indexing << " ms\nCold hierarchy queries ";
  time_queries(1);
  std::cout << "Warm hierarchy queries (30000) ";
  time_queries(10'000);
}

} // namespace

int main(int argc, char** argv) try {
  if (argc > 4) {
    throw std::runtime_error("Usage: hippocafe_benchmarks [class-file-or-directory] [rounds] [java-home]");
  }
  std::size_t rounds = 10;
  if (argc >= 3) {
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
  benchmark_analysis(200, 32, rounds);
  benchmark_analysis(2'000, 800, rounds);
  if (argc == 4)
    benchmark_hierarchy(argv[3]);
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
