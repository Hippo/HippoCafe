#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>
#include <cafe/class_rw.hpp>

TEST(class_model, test) {
  std::ifstream stream("data/ArrayTest.class", std::ios::binary);
  cafe::class_model model;
  stream >> cafe::class_reader(model, [](const std::exception& e) { std::cerr << e.what() << std::endl; });

  std::cout << model.to_string() << std::endl;
}
