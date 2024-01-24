#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>

TEST(class_reader, read) {
  std::ifstream stream("tests/data/HelloWorld.class", std::ios::binary);
  cafe::class_file cf;
  stream >> cf;

  ASSERT_EQ(cf.magic, 0xCAFEBABE);

  if (const auto cls_info = std::get_if<cafe::cp::class_info>(&cf.constant_pool[cf.this_class])) {
    if (const auto name = std::get_if<cafe::cp::utf8_info>(&cf.constant_pool[cls_info->name_index])) {
      ASSERT_EQ(name->value, "HelloWorld");
    } else {
      FAIL();
    }
  } else {
    FAIL();
  }
}