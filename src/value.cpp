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
} // namespace cafe
