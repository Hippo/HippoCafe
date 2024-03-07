#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>
#include <cafe/class_rw.hpp>
#include <cafe/constants.hpp>

TEST(class_model, test) {
  std::ifstream stream("data/CalculationTest.class", std::ios::binary);
  cafe::class_model model;
  stream >> cafe::class_reader(model, [](const std::exception& e) { std::cerr << e.what() << std::endl; });

  for (const auto& method : model.methods) {
    std::cout << "Method: " << method.name << " " << method.descriptor << std::endl;
    for (const auto& in : method.code) {
      std::cout << "  Opcode: " << cafe::opcode_name(opcode(in)) << std::endl;
    }
  }
}
