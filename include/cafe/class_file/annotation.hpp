#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cafe/apidef.hpp"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#pragma message("MESSAGE FROM annotation.hpp " TOSTRING(CAFE_API))

#ifdef CAFE_API
#pragma message("MESSAGE FROM annotation.hpp CAFE_API is defined")
#endif

namespace cafe::attribute {
class element_value;
struct element_pair;

class CAFE_API annotation {
public:
  uint16_t type_index{};
  std::vector<element_pair> elements;
};

class CAFE_API element_value {
public:
  uint8_t tag{};

  struct CAFE_API enum_value {
    uint16_t type_name_index;
    uint16_t const_name_index;
  };

  std::variant<uint16_t, enum_value, annotation, std::vector<element_value>> value;
};

struct CAFE_API element_pair {
  uint16_t name_index{};
  element_value value;
};

class CAFE_API type_parameter {
public:
  uint8_t index;
};

class CAFE_API supertype {
public:
  uint16_t index;
};

class CAFE_API type_parameter_bound {
public:
  uint8_t type_parameter_index;
  uint8_t bound_index;
};

class CAFE_API empty {};

class CAFE_API formal_parameter {
public:
  uint8_t index;
};

class CAFE_API throws {
public:
  uint16_t index;
};

class CAFE_API localvar {
public:
  struct CAFE_API local {
    uint16_t start_pc;
    uint16_t length;
    uint16_t index;
  };

  std::vector<local> table;
};

class CAFE_API catch_target {
public:
  uint16_t index;
};

class CAFE_API offset_target {
public:
  uint16_t offset;
};

class CAFE_API type_argument {
public:
  uint16_t offset;
  uint8_t index;
};

using type_annotation_target = std::variant<type_parameter, supertype, type_parameter_bound, empty, formal_parameter,
                                            throws, localvar, catch_target, offset_target, type_argument>;

class CAFE_API type_path {
public:
  struct CAFE_API path {
    uint8_t kind;
    uint8_t index;
  };

  std::vector<path> paths;
};

class CAFE_API type_annotation {
public:
  uint8_t target_type;
  type_annotation_target target_info;
  type_path target_path;
  uint16_t type_index;
  std::vector<element_pair> elements;
};
} // namespace cafe::attribute
