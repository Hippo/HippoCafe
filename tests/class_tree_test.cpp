#include <../include/cafe/cafe.hpp>
#include <gtest/gtest.h>

TEST(class_tree_test, assignable_from) {
  cafe::class_tree tree(cafe::load_rt);

  std::cout << tree.is_assignable_from("java/lang/Object", "[I") << std::endl;
}