#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <hippo/cafe.hpp>

TEST(class_writer, test) {
  std::string test_name = "ForLoopTest";
  std::ifstream stream(std::string("data/") + test_name + ".class", std::ios::binary);

  cafe::class_file file;
  cafe::class_reader reader(stream);
  reader.accept(file);
  std::cout << "Read: " << file.name << " writing..." << std::endl;
  cafe::class_writer writer;
  file.accept(writer);
  std::ofstream os(test_name + ".class", std::ios::binary);
  const auto data = writer.write();
  os.write(reinterpret_cast<const char*>(data.data()), data.size());
}
