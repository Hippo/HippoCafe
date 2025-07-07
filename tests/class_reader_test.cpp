#include <filesystem>
#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

TEST(class_reader, test) {
  std::string test_name = "TcbTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  const auto file_res = reader.read(stream);
  if (!file_res) {
    std::cerr << file_res.err().message() << std::endl;
    return;
  }
  const auto& file = file_res.value();

  std::cout << file.name << std::endl;
  for (const auto& m : file.methods) {
    std::cout << m.name << " " << m.desc << std::endl;
    for (const auto& in : m.body) {
      std::cout << "  " << cafe::to_string(in) << std::endl;
    }
  }
}