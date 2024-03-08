#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <cafe/class_file.hpp>
#include <cafe/class_rw.hpp>

TEST(class_model, test) {
  std::ifstream stream("data/AnnotationTest.class", std::ios::binary);
  cafe::class_model model;
  stream >> cafe::class_reader(model, [](const std::exception& e) { std::cerr << e.what() << std::endl; });

  std::cout << "Visible Annotations: " << std::endl;
  for (const auto& anno : model.visible_annotations) {
    std::cout << "  " << anno.to_string() << std::endl;
  }
  std::cout << "Visible Type Annotations: " << std::endl;
  for (const auto& anno : model.visible_type_annotations) {
    std::cout << "  " << anno.to_string() << std::endl;
  }
  std::cout << "Invisible Annotations: " << std::endl;
  for (const auto& anno : model.invisible_annotations) {
    std::cout << "  " << anno.to_string() << std::endl;
  }
  std::cout << "Invisible Type Annotations: " << std::endl;
  for (const auto& anno : model.invisible_type_annotations) {
    std::cout << "  " << anno.to_string() << std::endl;
  }
  for (const auto& method : model.methods) {
    std::cout << "Method: " << method.name << " " << method.descriptor << std::endl;
    for (const auto& in : method.code) {
      std::cout << "  " << cafe::to_string(in) << std::endl;
    }

    std::cout << "Visible Annotations: " << std::endl;
    for (const auto& anno : method.visible_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
    std::cout << " Visible Type Annotations: " << std::endl;
    for (const auto& anno : method.visible_type_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
    std::cout << "Invisible Annotations: " << std::endl;
    for (const auto& anno : method.invisible_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
    std::cout << "Invisible Type Annotations: " << std::endl;
    for (const auto& anno : method.invisible_type_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
    std::cout << "Code Visible Type Annotations: " << std::endl;
    for (const auto& anno : method.code.visible_type_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
    std::cout << "Code Invisible Type Annotations: " << std::endl;
    for (const auto& anno : method.code.invisible_type_annotations) {
      std::cout << "  " << anno.to_string() << std::endl;
    }
  }
}
