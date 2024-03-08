#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "value.hpp"

namespace cafe::cp {
class class_info {
public:
  static constexpr uint8_t tag = 7;
  uint16_t name_index;
};

class field_ref_info {
public:
  static constexpr uint8_t tag = 9;
  uint16_t class_index;
  uint16_t name_and_type_index;
};

class method_ref_info {
public:
  static constexpr uint8_t tag = 10;
  uint16_t class_index;
  uint16_t name_and_type_index;
};

class interface_method_ref_info {
public:
  static constexpr uint8_t tag = 11;
  uint16_t class_index;
  uint16_t name_and_type_index;
};

class string_info {
public:
  static constexpr uint8_t tag = 8;
  uint16_t string_index;
};


template<typename T, uint8_t tag_value>
class value_info {
public:
  static constexpr uint8_t tag = tag_value;
  T value;
};

using integer_info = value_info<int32_t, 3>;

using float_info = value_info<float, 4>;

using long_info = value_info<int64_t, 5>;

using double_info = value_info<double, 6>;

class name_and_type_info {
public:
  static constexpr uint8_t tag = 12;
  uint16_t name_index;
  uint16_t descriptor_index;
};

using utf8_info = value_info<std::string, 1>;

class method_handle_info {
public:
  static constexpr uint8_t tag = 15;
  uint8_t reference_kind;
  uint16_t reference_index;
};

class method_type_info {
public:
  static constexpr uint8_t tag = 16;
  uint16_t descriptor_index;
};

class dynamic_info {
public:
  static constexpr uint8_t tag = 17;
  uint16_t bootstrap_method_attr_index;
  uint16_t name_and_type_index;
};

class invoke_dynamic_info {
public:
  static constexpr uint8_t tag = 18;
  uint16_t bootstrap_method_attr_index;
  uint16_t name_and_type_index;
};

class module_info {
public:
  static constexpr uint8_t tag = 19;
  uint16_t name_index;
};

class package_info {
public:
  static constexpr uint8_t tag = 20;
  uint16_t name_index;
};

class pad_info {};

using constant_pool_info =
    std::variant<pad_info, class_info, field_ref_info, method_ref_info, interface_method_ref_info, string_info,
                 integer_info, float_info, long_info, double_info, name_and_type_info, utf8_info, method_handle_info,
                 method_type_info, dynamic_info, invoke_dynamic_info, module_info, package_info>;

class constant_pool : public std::vector<constant_pool_info> {
public:
  [[nodiscard]] std::string get_string(uint16_t index) const;
};
} // namespace cafe::cp