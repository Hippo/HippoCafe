#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>

TEST(class_file, io) {
  std::string test_name = "ForLoopTestLong";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);
  cafe::class_file cf;
  stream >> cf;

  ASSERT_EQ(cf.magic, 0xCAFEBABE);

  if (const auto cls_info = std::get_if<cafe::cp::class_info>(&cf.constant_pool[cf.this_class])) {
    if (const auto name = std::get_if<cafe::cp::utf8_info>(&cf.constant_pool[cls_info->name_index])) {
      std::cout << "Class: " << name->value << std::endl;
      ASSERT_EQ(name->value, test_name);
    } else {
      FAIL();
    }
  } else {
    FAIL();
  }


  std::ofstream out_stream(test_name + ".class", std::ios::binary);
  out_stream << cf;
}
