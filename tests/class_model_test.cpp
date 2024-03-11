#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

TEST(class_model, test) {
  std::ifstream stream("data/ArrayTest.class", std::ios::binary);
  cafe::class_model model;
  stream >> cafe::class_reader(model, [](const std::exception& e) { std::cerr << e.what() << std::endl; });

  std::cout << model.to_string() << std::endl;
}

TEST(class_model, rw) {
  std::string test_name = "GraxCrackMe";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);
  cafe::class_model model;
  stream >> model;

  std::cout << "Read class " << model.name << " writing..." << std::endl;

  std::cout << model.to_string() << std::endl;

  std::ofstream out_stream(test_name + ".class", std::ios::binary);
  out_stream << model;

  out_stream.close();
  std::ifstream in(test_name + ".class", std::ios::binary);
  cafe::class_file cf2;
  in >> cf2;
  std::cout << "read back: " << cf2.constant_pool.get_string(cf2.this_class) << std::endl;
}
