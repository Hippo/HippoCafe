#include "cafe/value.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "cafe/constants.hpp"

namespace cafe {

class_value::class_value(const std::string_view& value) {
  auto internal = std::string(value);
  auto binary = internal;
  std::replace(internal.begin(), internal.end(), '.', '/');
  std::replace(binary.begin(), binary.end(), '/', '.');
  this->internal_ = internal;
  this->binary_ = binary;
}
std::string class_value::get() const {
  return internal_;
}
std::string class_value::binary() const {
  return binary_;
}
void class_value::set(const std::string_view& value) {
  auto internal = std::string(value);
  auto binary = internal;
  std::replace(internal.begin(), internal.end(), '.', '/');
  std::replace(binary.begin(), binary.end(), '/', '.');
  this->internal_ = internal;
  this->binary_ = binary;
}
method_handle::method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                             const std::string_view& descriptor) :
    kind(kind), owner(owner), name(name), descriptor(descriptor) {
}
method_type::method_type(const std::string_view& descriptor) : descriptor(descriptor) {
}
dynamic::dynamic(const std::string_view& name, const std::string_view& descriptor, method_handle handle,
                 const std::vector<value>& args) :
    name(name), descriptor(descriptor), handle(std::move(handle)), args(args) {
}
std::string to_string(const value& v) {
  return std::visit(
      [](const auto& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, class_value>) {
          return arg.get();
        } else if constexpr (std::is_same_v<T, std::string>) {
          std::ostringstream oss;
          oss << '"' << arg << '"';
          return oss.str();
        } else if constexpr (std::is_same_v<T, method_handle>) {
          std::ostringstream oss;
          oss << "method_handle(" << reference_kind_name(arg.kind) << ", " << '"' << arg.owner << '"' << ", " << '"'
              << arg.name << '"' << ", " << '"' << arg.descriptor << '"' << ")";
          return oss.str();
        } else if constexpr (std::is_same_v<T, method_type>) {
          return "method_type(\"" + arg.descriptor + "\")";
        } else if constexpr (std::is_same_v<T, dynamic>) {
          std::ostringstream oss;
          oss << "dynamic(" << '"' << arg.name << '"' << ", " << '"' << arg.descriptor << '"' << ", "
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

class_descriptor::class_descriptor(const std::string_view& name) : descriptor_(name) {
  update();
}
std::string class_descriptor::to_string() const {
  return descriptor_;
}
uint8_t class_descriptor::size() const {
  return 1;
}
uint8_t class_descriptor::dims() const {
  return dims_;
}
bool class_descriptor::is_array() const {
  return dims_ > 0;
}
void class_descriptor::set(const std::string_view& name) {
  descriptor_ = name;
  update();
}
void class_descriptor::update() {
  dims_ = 0;
  for (const auto& c : descriptor_) {
    if (c == '[') {
      dims_++;
    } else {
      break;
    }
  }
}
std::string to_string(const descriptor& d) {
  return std::visit([](const auto& arg) -> std::string { return arg.to_string(); }, d);
}
uint8_t size(const descriptor& d) {
  return std::visit([](const auto& arg) -> uint8_t { return arg.size(); }, d);
}
descriptor parse_descriptor(const std::string_view& descriptor) {
  switch (descriptor[0]) {
    case 'V':
      return void_descriptor();
    case 'Z':
      return boolean_descriptor();
    case 'B':
      return byte_descriptor();
    case 'C':
      return char_descriptor();
    case 'S':
      return short_descriptor();
    case 'I':
      return int_descriptor();
    case 'J':
      return long_descriptor();
    case 'F':
      return float_descriptor();
    case 'D':
      return double_descriptor();
    case '[':
    case 'L': {
      std::string internal;
      for (const auto& c : descriptor) {
        internal.push_back(c);
        if (c == ';') {
          break;
        }
      }
      return class_descriptor(internal);
    }
    default:
      throw std::invalid_argument("invalid descriptor");
  }
}
std::pair<std::vector<descriptor>, descriptor> parse_method_descriptor(const std::string_view& descriptor) {
  std::vector<cafe::descriptor> args;
  std::string_view::size_type i = 1;
  while (descriptor[i] != ')') {
    std::string_view::size_type j = i;
    if (descriptor[j] == 'L' || descriptor[j] == '[') {
      j = descriptor.find(';', j);
    }
    args.push_back(parse_descriptor(descriptor.substr(i, j - i + 1)));
    i = j + 1;
  }
  return {args, parse_descriptor(descriptor.substr(i + 1))};
}
} // namespace cafe
