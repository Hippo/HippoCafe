#include <filesystem>
#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <../include/cafe/cafe.hpp>

TEST(class_reader, test) {
  std::string test_name = "ArrayTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  const auto file = reader.read(stream);

  std::cout << file.name << std::endl;
  for (const auto& m : file.methods) {
    std::cout << m.name << " " << m.desc << std::endl;
    for (const auto& in : m.code) {
      std::cout << "  " << cafe::to_string(in) << std::endl;
    }
  }
}

TEST(crasher, test) {
  for (const auto& path : std::filesystem::directory_iterator("data/crasher")) {
    if (path.path().extension() == ".class") {
      std::ifstream stream(path.path(), std::ios::binary);
      cafe::class_reader reader;
      const auto file = reader.read(stream);
      std::cout << file.name << std::endl;
      for (const auto& method : file.methods) {
        std::cout << method.name_desc(" ") << std::endl;
      }
    }
  }
}
