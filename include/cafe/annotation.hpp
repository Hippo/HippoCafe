#pragma once

#include <variant>
#include <vector>

namespace cafe::attribute {
class element_value;
struct element_pair;

class annotation {
public:
  uint16_t type_index;
  std::vector<element_pair> elements;
};

class element_value {
public:
  uint8_t tag;

  struct enum_value {
    uint16_t type_name_index;
    uint16_t const_name_index;
  };

  std::variant<uint16_t, enum_value, annotation, std::vector<element_value>> value;
};

struct element_pair {
  uint16_t name_index;
  element_value value;
};

class type_parameter {
public:
  uint8_t index;
};

class supertype {
public:
  uint16_t index;
};

class type_parameter_bound {
public:
  uint8_t type_parameter_index;
  uint8_t bound_index;
};

class empty {};

class formal_parameter {
public:
  uint8_t index;
};

class throws {
public:
  uint16_t index;
};

class localvar {
public:
  struct local {
    uint16_t start_pc;
    uint16_t length;
    uint16_t index;
  };

  std::vector<local> table;
};

class catch_target {
public:
  uint16_t index;
};

class offset_target {
public:
  uint16_t offset;
};

class type_argument {
public:
  uint16_t offset;
  uint8_t index;
};

using type_annotation_target = std::variant<type_parameter, supertype, type_parameter_bound, empty, formal_parameter,
  throws, localvar, catch_target, offset_target, type_argument>;

class type_path {
public:
  struct path {
    uint8_t kind;
    uint8_t index;
  };

  std::vector<path> paths;
};

class type_annotation {
public:
  uint8_t target_type;
  type_annotation_target target_info;
  type_path target_path;
  uint16_t type_index;
  std::vector<element_pair> elements;
};
}

