#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "apidef.hpp"

namespace cafe {

enum class CAFE_API type_kind : uint8_t {
  void_,
  boolean,
  byte,
  char_,
  short_,
  int_,
  long_,
  float_,
  double_,
  object,
  array,
  method,
  internal
};

class CAFE_API type {
public:
  type(const std::string_view& value);
  type(type_kind kind, const std::string_view& value);
  ~type() = default;
  type(const type&) = default;
  type(type&&) = default;
  type& operator=(const type&) = default;
  type& operator=(type&&) = default;
  bool operator==(const type& other) const;
  bool operator!=(const type& other) const;
  std::vector<type> parameter_types() const;
  type return_type() const;
  type array_type() const;
  uint8_t dimensions() const;
  uint8_t size() const;

  std::string_view internal() const;
  std::string desc() const;

  type_kind kind() const;
  const std::string& get() const;

  static type_kind get_kind(const std::string_view& value);

private:
  std::string value_;
  type_kind kind_;
};


class CAFE_API method_handle {
public:
  uint8_t kind{};
  std::string owner;
  std::string name;
  std::string desc;
  bool interface{};
  method_handle() = default;
  method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                const std::string_view& desc);
  method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name, const std::string_view& desc,
                bool interface);
  ~method_handle() = default;
  method_handle(const method_handle&) = default;
  method_handle(method_handle&&) = default;
  method_handle& operator=(const method_handle&) = default;
  method_handle& operator=(method_handle&&) = default;

  bool operator==(const method_handle& other) const;
  bool operator!=(const method_handle& other) const;
};

class CAFE_API method_type {
public:
  std::string desc;
  method_type() = default;
  method_type(const std::string_view& desc);
  ~method_type() = default;
  method_type(const method_type&) = default;
  method_type(method_type&&) = default;
  method_type& operator=(const method_type&) = default;
  method_type& operator=(method_type&&) = default;

  bool operator==(const method_type& other) const;
  bool operator!=(const method_type& other) const;
};

class dynamic;
using value = std::variant<int32_t, float, int64_t, double, type, std::string, method_handle, method_type, dynamic>;
std::string to_string(const value& v);

class CAFE_API dynamic {
public:
  std::string name;
  std::string desc;
  method_handle handle;
  std::vector<value> args;
  dynamic() = default;
  dynamic(const std::string_view& name, const std::string_view& desc, method_handle handle,
          const std::vector<value>& args);
  ~dynamic() = default;
  dynamic(const dynamic&) = default;
  dynamic(dynamic&&) = default;
  dynamic& operator=(const dynamic&) = default;
  dynamic& operator=(dynamic&&) = default;

  bool operator==(const dynamic& other) const;
  bool operator!=(const dynamic& other) const;
};

} // namespace cafe
