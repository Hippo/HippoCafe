#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>

#include "attribute.hpp"
#include "constant_pool.hpp"

namespace cafe {
class method_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class field_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class class_file {
public:
  uint32_t magic;
  uint16_t minor_version;
  uint16_t major_version;
  cp::constant_pool constant_pool;
  uint16_t access_flags;
  uint16_t this_class;
  uint16_t super_class;
  std::vector<uint16_t> interfaces;
  std::vector<field_info> fields;
  std::vector<method_info> methods;
  std::vector<attribute::attribute> attributes;
};

std::istream& operator>>(std::istream& stream, class_file& file);

std::ostream& operator<<(std::ostream& stream, const class_file& file);
}