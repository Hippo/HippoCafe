#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "cafe/value.hpp"
#include "attribute.hpp"

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


class bsm_buffer;

class constant_pool : public std::vector<constant_pool_info> {
public:
  [[nodiscard]] std::string get_string(uint16_t index) const;

  uint16_t get_utf(const std::string_view& value);
  uint16_t get_class(const std::string_view& value);
  uint16_t get_string(const std::string_view& value);
  uint16_t get_module(const std::string_view& value);
  uint16_t get_package(const std::string_view& value);
  uint16_t get_value(bsm_buffer& bsm_buffer, const value& value);
  uint16_t get_name_and_type(const std::string_view& name, const std::string_view& descriptor);
  uint16_t get_field_ref(const std::string_view& class_name, const std::string_view& name,
                         const std::string_view& descriptor);
  uint16_t get_method_ref(const std::string_view& class_name, const std::string_view& name,
                          const std::string_view& descriptor);
  uint16_t get_interface_method_ref(const std::string_view& class_name, const std::string_view& name,
                                    const std::string_view& descriptor);
  [[nodiscard]] uint16_t count() const;
};

class bsm_buffer {
public:
  uint16_t get_bsm_index(constant_pool& pool, const method_handle& handle, const std::vector<value>& args);
  std::vector<attribute::bootstrap_methods::bootstrap_method> bsms;
};

} // namespace cafe::cp
