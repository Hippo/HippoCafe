#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

#include <hippo/cafe.hpp>
#include <zip.h>

void read_zip(cafe::class_tree& tree, const std::string_view& path) {
  int err = 0;
  zip_t* archive = zip_open(path.data(), ZIP_RDONLY, &err);
  if (!archive) {
    zip_error_t zip_error;
    zip_error_init_with_code(&zip_error, err);
    std::string message = zip_error_strerror(&zip_error);
    zip_error_fini(&zip_error);
    throw std::runtime_error("Failed to open ZIP archive: " + message);
  }

  const auto num_entries = zip_get_num_entries(archive, 0);
  for (auto i = 0; i < num_entries; i++) {
    const char* name = zip_get_name(archive, i, 0);
    if (!name) continue;

    zip_file_t* file = zip_fopen_index(archive, i, 0);
    if (!file) continue;

    zip_stat_t stat;
    zip_stat_index(archive, i, 0, &stat);

    std::vector<int8_t> content(stat.size);
    zip_fread(file, content.data(), stat.size);
    zip_fclose(file);

    if (std::string(name).find(".class") != std::string::npos) {
      cafe::class_reader reader;
      if (const auto res = reader.read(content)) {
        tree.put(res.value());
      } else {
        std::cerr << res.error_message() << std::endl;
      }
    }
  }
}

void write_gen(cafe::class_tree& tree) {
  if (!std::filesystem::exists("gen-out")) {
    std::filesystem::create_directory("gen-out");
  }

  auto unit = 0;
  auto func = 0;
  auto count = 0;
  std::fstream file("gen-out/gen_class_tree_0.cpp", std::ios::out);
  file << "#include\"gen_class_tree.hpp\"\n\nnamespace cafe {\nvoid put_0_0(class_tree& tree) {\n";
  for (const auto& [name, node] : tree) {
    std::ostringstream ss;
    ss << "  tree.put(\"" << name << "\", ";
    if (node->super_class) {
      ss << "\"" << node->super_class->name << "\", {";
    } else {
      ss << "std::nullopt, {";
    }
    bool first = true;
    for (const auto& interface : node->interfaces) {
      if (!first) {
        ss << ", ";
      }
      ss << "\"" << interface->name << "\"";
      first = false;
    }
    ss << "});\n";
    count++;
    if (count >= 32) {
      ss << "}\n";
      func++;
      count = 0;
      if (func < 32) {
        ss << "void put_" << unit << "_" << func << "(class_tree& tree) {\n";
      } else {
        ss << "}\n";
      }
    }
    file << ss.str();
    if (func >= 32) {
      func = 0;
      unit++;
      const auto unit_str = std::to_string(unit);
      file = std::fstream("gen-out/gen_class_tree_" + unit_str + ".cpp", std::ios::out);
      file << "#include\"gen_class_tree.hpp\"\n\nnamespace cafe {\nvoid put_" << unit_str << "_0(class_tree& tree) {\n";
    }
  }
  file << "}\n}\n";
  file = std::fstream("gen-out/gen_class_tree.hpp", std::ios::out);
  file << "#pragma once\n\n#include\"cafe/class_tree.hpp\"\n\nnamespace cafe {\n";
  file << "void put_rt(class_tree& tree);\n";
  for (auto u = 0; u < unit; u++) {
    for (auto f = 0; f < 32; f++) {
      file << "void put_" << u << "_" << f << "(class_tree& tree);\n";
    }
  }
  for (auto f = 0; f <= func; f++) {
    file << "void put_" << unit << "_" << f << "(class_tree& tree);\n";
  }
  file << "}\n";

  file = std::fstream("gen-out/gen_class_tree.cpp", std::ios::out);
  file << "#include\"gen_class_tree.hpp\"\n\nnamespace cafe {\nvoid put_rt(class_tree& tree) {\n";
  for (auto u = 0; u < unit; u++) {
    for (auto f = 0; f < 32; f++) {
      file << "  put_" << u << "_" << f << "(tree);\n";
    }
  }
  for (auto f = 0; f <= func; f++) {
    file << "  put_" << unit << "_" << f << "(tree);\n";
  }
  file << "}\n}\n";

  file = std::fstream("gen-out/cmake-snippet.txt", std::ios::out);
  std::ostringstream ss;
  ss << "set(CAFE_GEN_SOURCES\n  src/gen/gen_class_tree.cpp src/gen/gen_class_tree.hpp\n";
  for (auto u = 0; u <= unit; u++) {
    ss << "  src/gen/gen_class_tree_" << u << ".cpp\n";
  }
  ss << ")\n";
  file << ss.str();
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <path>" << std::endl;
    return 1;
  }
  std::string path = argv[1];
  cafe::class_tree tree;
  try {
    read_zip(tree, path);
    write_gen(tree);
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
  }
}