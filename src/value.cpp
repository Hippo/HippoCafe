#include "cafe/value.hpp"

#include <algorithm>
#include <utility>

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
} // namespace cafe
