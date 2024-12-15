#include "cafe/value.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "cafe/constants.hpp"

namespace cafe {
method_handle::method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                             const std::string_view& desc) : kind(kind), owner(owner), name(name), desc(desc), interface(kind == reference_kind::invoke_interface) {
}
method_handle::method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                             const std::string_view& desc, bool interface) : kind(kind), owner(owner), name(name),
                                                                           desc(desc), interface(interface) {
}
method_type::method_type(const std::string_view& desc) : desc(desc) {
}
dynamic::dynamic(const std::string_view& name, const std::string_view& desc, method_handle handle,
                 const std::vector<value>& args) :
    name(name), desc(desc), handle(std::move(handle)), args(args) {
}
std::string to_string(const value& v) {
  return std::visit(
      [](const auto& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, type>) {
          return arg.get();
        } else if constexpr (std::is_same_v<T, std::string>) {
          std::ostringstream oss;
          oss << '"' << arg << '"';
          return oss.str();
        } else if constexpr (std::is_same_v<T, method_handle>) {
          std::ostringstream oss;
          oss << "method_handle(" << reference_kind_name(arg.kind) << ", " << '"' << arg.owner << '"' << ", " << '"'
              << arg.name << '"' << ", " << '"' << arg.desc << '"' << ")";
          return oss.str();
        } else if constexpr (std::is_same_v<T, method_type>) {
          return "method_type(\"" + arg.desc + "\")";
        } else if constexpr (std::is_same_v<T, dynamic>) {
          std::ostringstream oss;
          oss << "dynamic(" << '"' << arg.name << '"' << ", " << '"' << arg.desc << '"' << ", "
              << to_string(arg.handle) << ", [";
          bool first = true;
          for (const auto& a : arg.args) {
            if (!first) {
              oss << ", ";
            }
            oss << to_string(a);
            first = false;
          }
          oss << "])";
          return oss.str();
        } else {
          return std::to_string(arg);
        }
      },
      v);
}

type::type(const std::string_view& value) : value_(value), kind_(get_kind(value)) {
}

type::type(type_kind kind, const std::string_view& value) : value_(value), kind_(kind) {
}
bool type::operator==(const type& other) const {
  return kind() == other.kind() && internal() == other.internal();
}
bool type::operator!=(const type& other) const {
  return !(*this == other);
}
std::vector<type> type::parameter_types() const {
  if (kind_ != type_kind::method || value_.empty() || value_[0] != '(') {
    return {};
  }
  std::vector<type> types;
  size_t i = 1;
  while (value_[i] != ')') {
    size_t j = i;
    while (value_[j] == '[') {
      j++;
    }
    if (value_[j] == 'L') {
      j = value_.find(';', j);
    }
    types.emplace_back(value_.substr(i, j - i + 1));
    i = j + 1;
  }
  return types;
}

type type::return_type() const {
  if (kind_ != type_kind::method) {
    return *this;
  }
  const auto i = value_.find(')') + 1;
  return {value_.substr(i)};
}

type type::array_type() const {
  if (kind_ != type_kind::array || value_.empty() || value_[0] != '[') {
    return *this;
  }
  return {value_.substr(1)};
}

uint8_t type::dimensions() const {
  auto i = 0;
  if (!value_.empty()) {
    while (value_[i] == '[') {
      i++;
    }
  }
  return i;
}

uint8_t type::size() const {
  switch (kind_) {
    case type_kind::void_:
      return 0;
    case type_kind::long_:
    case type_kind::double_:
      return 2;
    default:
      return 1;
  }
}

std::string_view type::internal() const {
  if (kind_ != type_kind::object || value_.length() < 3) {
    return value_;
  }
  return {value_.data() + 1, value_.length() - 2};
}

std::string type::desc() const {
  if (kind_ != type_kind::internal) {
    return value_;
  }
  std::string res;
  res.reserve(value_.size() + 2);
  res.push_back('L');
  res.append(value_);
  res.push_back(';');
  return res;
}

type_kind type::kind() const {
  return kind_ == type_kind::internal ? type_kind::object : kind_;
}
const std::string& type::get() const {
  return value_;
}
type_kind type::get_kind(const std::string_view& value) {
  switch (value.empty() ? 0 : value[0]) {
    case 'V':
      return type_kind::void_;
    case 'Z':
      return type_kind::boolean;
    case 'B':
      return type_kind::byte;
    case 'C':
      return type_kind::char_;
    case 'S':
      return type_kind::short_;
    case 'I':
      return type_kind::int_;
    case 'J':
      return type_kind::long_;
    case 'F':
      return type_kind::float_;
    case 'D':
      return type_kind::double_;
    case '(':
      return type_kind::method;
    case '[':
      return type_kind::array;
    case 'L':
      return type_kind::object;
    default:
      return type_kind::internal;
  }
}
} // namespace cafe
