#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "apidef.hpp"

namespace cafe {

class CAFE_API class_value {
public:
  class_value() = default;
  class_value(const std::string_view& value);
  ~class_value() = default;
  class_value(const class_value&) = default;
  class_value(class_value&&) = default;
  class_value& operator=(const class_value&) = default;
  class_value& operator=(class_value&&) = default;
  [[nodiscard]] std::string get() const;
  [[nodiscard]] std::string binary() const;
  void set(const std::string_view& value);

private:
  std::string internal_;
  std::string binary_;
};

class CAFE_API method_handle {
public:
  uint8_t kind{};
  std::string owner;
  std::string name;
  std::string descriptor;
  method_handle() = default;
  method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                const std::string_view& descriptor);
  ~method_handle() = default;
  method_handle(const method_handle&) = default;
  method_handle(method_handle&&) = default;
  method_handle& operator=(const method_handle&) = default;
  method_handle& operator=(method_handle&&) = default;
};

class CAFE_API method_type {
public:
  std::string descriptor;
  method_type() = default;
  method_type(const std::string_view& descriptor);
  ~method_type() = default;
  method_type(const method_type&) = default;
  method_type(method_type&&) = default;
  method_type& operator=(const method_type&) = default;
  method_type& operator=(method_type&&) = default;
};

class dynamic;
using value =
    std::variant<int32_t, float, int64_t, double, class_value, std::string, method_handle, method_type, dynamic>;
std::string to_string(const value& v);

class CAFE_API dynamic {
public:
  std::string name;
  std::string descriptor;
  method_handle handle;
  std::vector<value> args;
  dynamic() = default;
  dynamic(const std::string_view& name, const std::string_view& descriptor, method_handle handle,
          const std::vector<value>& args);
  ~dynamic() = default;
  dynamic(const dynamic&) = default;
  dynamic(dynamic&&) = default;
  dynamic& operator=(const dynamic&) = default;
  dynamic& operator=(dynamic&&) = default;
};


template<char C, uint8_t S = 1>
class CAFE_API basic_descriptor {
public:
  ~basic_descriptor() = default;
  [[nodiscard]] std::string to_string() const {
    return {C};
  }
  [[nodiscard]] uint8_t size() const {
    return S;
  }
};
using void_descriptor = basic_descriptor<'V'>;
using boolean_descriptor = basic_descriptor<'Z'>;
using byte_descriptor = basic_descriptor<'B'>;
using char_descriptor = basic_descriptor<'C'>;
using short_descriptor = basic_descriptor<'S'>;
using int_descriptor = basic_descriptor<'I'>;
using long_descriptor = basic_descriptor<'J', 2>;
using float_descriptor = basic_descriptor<'F'>;
using double_descriptor = basic_descriptor<'D', 2>;
class CAFE_API class_descriptor {
public:
  class_descriptor() = default;
  explicit class_descriptor(const std::string_view& name);
  ~class_descriptor() = default;
  class_descriptor(const class_descriptor&) = default;
  class_descriptor(class_descriptor&&) = default;
  class_descriptor& operator=(const class_descriptor&) = default;
  class_descriptor& operator=(class_descriptor&&) = default;
  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] uint8_t size() const;
  [[nodiscard]] uint8_t dims() const;
  [[nodiscard]] bool is_array() const;
  void set(const std::string_view& name);

private:
  std::string descriptor_;
  uint8_t dims_{};
  void update();
};

using descriptor = std::variant<void_descriptor, boolean_descriptor, byte_descriptor, char_descriptor, short_descriptor,
                                int_descriptor, long_descriptor, float_descriptor, double_descriptor, class_descriptor>;
CAFE_API std::string to_string(const descriptor& d);
CAFE_API uint8_t size(const descriptor& d);
CAFE_API descriptor parse_descriptor(const std::string_view& descriptor);
CAFE_API std::pair<std::vector<descriptor>, descriptor> parse_method_descriptor(const std::string_view& descriptor);
} // namespace cafe
