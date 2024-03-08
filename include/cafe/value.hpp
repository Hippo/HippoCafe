#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cafe {

class class_value {
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

class method_handle {
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

class method_type {
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
using  value =
    std::variant<int32_t, float, int64_t, double, class_value, std::string, method_handle, method_type, dynamic>;
std::string to_string(const value& v);

class dynamic {
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


} // namespace cafe
