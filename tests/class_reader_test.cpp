#include <filesystem>
#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

TEST(class_reader, test) {
  std::string test_name = "GraxCrackMe";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  cafe::class_reader reader(stream);
  reader.accept(file);

  std::cout << file.name << std::endl;
  for (const auto& m : file.methods) {
    std::cout << m.name << " " << m.descriptor << std::endl;
    for (const auto& in : m.code) {
      std::cout << "  " << cafe::to_string(in) << std::endl;
    }
  }
}

TEST(class_file, overload_read) {
  std::string test_name = "LookupSwitchTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  stream >> file;
  std::cout << file.name << std::endl;
  for (const auto& m : file.methods) {
    std::cout << m.name << " " << m.descriptor << std::endl;
    for (const auto& in : m.code) {
      std::cout << "  " << cafe::to_string(in) << std::endl;
    }
  }

  std::ofstream os(test_name + ".class", std::ios::binary);
  os << file;
}

TEST(crasher, test) {
  for (const auto& path : std::filesystem::directory_iterator("data/crasher")) {
    if (path.path().extension() == ".class") {
      std::ifstream stream(path.path(), std::ios::binary);
      cafe::class_file file;
      cafe::class_reader reader(stream);
      reader.accept(file);
      std::cout << file.name << std::endl;
      for (const auto& method : file.methods) {
        std::cout << method.name << " " << method.descriptor << std::endl;
      }
    }
  }
}
