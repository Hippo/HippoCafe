#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

TEST(class_writer, test) {
  std::string test_name = "DoublePrint";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_reader reader;
  const auto file_res = reader.read(stream);
  if (!file_res) {
    std::cerr << file_res.error().message() << std::endl;
    return;
  }
  const auto& file = file_res.value();

  for (const auto& anno : file.visible_annotations) {
    std::cout << "Visible annotation: " << anno.to_string() << std::endl;
  }

  cafe::class_writer writer;
  std::ofstream os(test_name + ".class", std::ios::binary);
  const auto data = writer.write(file);
  os.write(reinterpret_cast<const char*>(data.data()), data.size());
  os.close();

  for (const auto& method : file.methods) {
    std::cout << method.name_desc() << std::endl;
    for (const auto& insn : method.code) {
      std::cout << "  " << cafe::to_string(insn) << std::endl;
    }
    for (const auto& [label, frame] : method.code.frames) {
      std::cout << "  > " << label.to_string() << " " << cafe::to_string(frame) << std::endl;
    }
  }

  std::cout << "read2: " << std::endl << std::endl;

  cafe::class_reader reader2;
  const auto file2_res = reader2.read(data);
  if (!file2_res) {
    std::cerr << file2_res.error().message() << std::endl;
    return;
  }
  const auto& file2 = file2_res.value();

  for (const auto& anno : file2.visible_annotations) {
    std::cout << "Visible annotation: " << anno.to_string() << std::endl;
  }

  for (const auto& method : file2.methods) {

    std::cout << method.name_desc() << std::endl;
      for (const auto& insn : method.code) {
        std::cout << "  " << cafe::to_string(insn) << std::endl;
      }
    for (const auto& [label, frame] : method.code.frames) {
      std::cout << "  > " << label.to_string() << " " << cafe::to_string(frame) << std::endl;
    }
  }
}
