#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>

#include "attribute.hpp"
#include "cafe/apidef.hpp"
#include "constant_pool.hpp"

namespace cafe {
class CAFE_API method_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class CAFE_API field_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class CAFE_API class_file {
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

CAFE_API std::istream& operator>>(std::istream& stream, class_file& file);

CAFE_API std::ostream& operator<<(std::ostream& stream, const class_file& file);
} // namespace cafe
