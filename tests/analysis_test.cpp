#include <fstream>

#include <../include/cafe/cafe.hpp>
#include <gtest/gtest.h>

TEST(block_graph, split) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  stream >> file;

  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.code);
    for (const auto& b : graph.blocks()) {
      std::cout << b.to_string() << std::endl;
    }
    std::cout << std::endl;
  }
}

TEST(block_graph, compute_maxes) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  stream >> file;

  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.code);
    std::cout << "before: " << method.code.max_locals << " " << method.code.max_stack << std::endl;
    graph.compute_maxes(method);
    std::cout << "after: " << method.code.max_locals << " " << method.code.max_stack << std::endl;
    std::cout << std::endl;
  }
}


TEST(block_graph, compute_frames) {
  std::string test_name = "SwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  stream >> file;
  cafe::class_tree tree(cafe::load_rt);


  for (auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    cafe::basic_block_graph graph(method.code);
    std::cout << "before: (" << method.code.frames.size() << ")" << std::endl;
    for (const auto& [lbl, frame] : method.code.frames) {
      std::cout << lbl.to_string() << " " << cafe::to_string(frame) << std::endl;
    }
    const auto result = graph.compute_frames(tree, file.name, cafe::basic_block_graph::get_start_locals(file.name, method));
    std::cout << "after: (" << result.frames().size() << ")" << std::endl;
    for (const auto& [lbl, frame] : result.frames()) {
      std::cout << lbl.to_string() << " " << cafe::to_string(frame) << std::endl;
    }
    std::cout << std::endl;
  }
}