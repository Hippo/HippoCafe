#include <gtest/gtest.h>

#include <../include/cafe/cafe.hpp>

static void test_desc(const std::string_view& desc) {
  cafe::type t(desc);
  std::cout << desc << " -> " << t.get() << std::endl;
  const auto params = t.parameter_types();
  const auto ret = t.return_type();
  std::cout << "Params (" << params.size() << "):" << std::endl;
  for (const auto& p : params) {
    std::cout << "  " << p.get() << std::endl;
  }
  std::cout << "Return: " << ret.get() << std::endl;
}

TEST(desc, parse) {
  std::string desc1 = "(Ljava/lang/String;IJJD[[Ljava/lang/String;)V";
  std::string desc2 = "(Ljava/lang/String;IJ[[JD[[Ljava/lang/String;)Ljava/lang/String;";
  std::string desc3 = "()V";

  std::cout << "desc1: " << desc1 << std::endl;
  test_desc(desc1);
  std::cout << "desc2: " << desc2 << std::endl;
  test_desc(desc2);
  std::cout << "desc3: " << desc3 << std::endl;
  test_desc(desc3);
}
