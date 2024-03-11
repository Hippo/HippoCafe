#include <cafe/value.hpp>
#include <gtest/gtest.h>

static void test_desc(const std::string_view& desc) {
  auto [args, return_type] = cafe::parse_method_descriptor(desc);
  for (const auto& arg : args) {
    std::cout << cafe::to_string(arg) << std::endl;
  }
  std::cout << "returns: " << cafe::to_string(return_type) << std::endl;
}

TEST(desc, parse) {
  std::string desc1 = "(Ljava/lang/String;IJJD[[Ljava/lang/String;)V";
  std::string desc2 = "(Ljava/lang/String;IJJD[[Ljava/lang/String;)Ljava/lang/String;";
  std::string desc3 = "()V";

  std::cout << "desc1: " << desc1 << std::endl;
  test_desc(desc1);
  std::cout << "desc2: " << desc2 << std::endl;
  test_desc(desc2);
  std::cout << "desc3: " << desc3 << std::endl;
  test_desc(desc3);
}
