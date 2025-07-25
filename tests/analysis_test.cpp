#include <fstream>

#include <gtest/gtest.h>
#include <hippo/cafe.hpp>

TEST(block_graph, split) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  const auto file_res = reader.read(stream);
  if (!file_res) {
    std::cerr << file_res.err().message() << std::endl;
    return;
  }
  const auto& file = file_res.value();

  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.body);
    for (const auto& b : graph.blocks()) {
      std::cout << b.to_string() << std::endl;
    }
    std::cout << std::endl;
  }
}

TEST(block_graph, compute_maxes) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  auto file_res = reader.read(stream);
  if (!file_res) {
    std::cerr << file_res.err().message() << std::endl;
    return;
  }
  auto& file = file_res.value();

  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.body);
    std::cout << "before: " << method.body.max_locals << " " << method.body.max_stack << std::endl;
    graph.compute_maxes(method);
    std::cout << "after: " << method.body.max_locals << " " << method.body.max_stack << std::endl;
    std::cout << std::endl;
  }
}


TEST(block_graph, compute_frames) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  const auto file_res = reader.read(stream);
  if (!file_res) {
    std::cerr << file_res.err().message() << std::endl;
    return;
  }
  const auto& file = file_res.value();
  cafe::class_tree tree(cafe::load_rt);


  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.body);
    std::cout << "before: (" << method.body.frames.size() << ")" << std::endl;
    for (const auto& [lbl, frame] : method.body.frames) {
      std::cout << cafe::to_string(lbl) << " " << cafe::to_string(frame) << std::endl;
    }
    const auto result =
        graph.compute_frames(tree, file.name, cafe::basic_block_graph::get_start_locals(file.name, method));
    std::cout << "after: (" << result.frames().size() << ")" << std::endl;
    for (const auto& [lbl, frame] : result.frames()) {
      std::cout << lbl.to_string() << " " << cafe::to_string(frame) << std::endl;
    }
    std::cout << std::endl;
  }
}

using namespace cafe;

TEST(exaple, test) {
  // The source of the class file, can either be a std::vector<int8_t> or std::istream.
  std::ifstream source("data/HelloWorld.class", std::ios::binary);

  class_reader reader;
  result<class_file> read_result = reader.read(source);
  if (!read_result) {
    std::cerr << read_result.err().message() << std::endl;
    return;
  }
  class_file& file = read_result.value();

  for (auto& method : file.methods) {
    for (auto& insn : method.body) {
      if (auto* push = std::get_if<push_insn>(&insn)) {
        if (push->operand == value{"Hello World"}) {
          push->operand = "Modified";
        }
      }
    }
  }

  class_writer writer;
  std::vector<int8_t> data = writer.write(file);
  std::ofstream os("HelloWorld.class", std::ios::binary);
  os.write(reinterpret_cast<const char*>(data.data()), data.size());
}
