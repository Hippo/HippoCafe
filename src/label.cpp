#include "cafe/label.hpp"

#include <sstream>

namespace cafe {
label::label() : id_(reinterpret_cast<uintptr_t>(this)) {
}
bool label::operator==(const label& other) const {
  return id_ == other.id_;
}
uint64_t label::id() const {
  return id_;
}
label::label(const label& other) {
  id_ = other.id_;
  debug_name = other.debug_name;
}
std::string label::to_string() const {
  std::ostringstream oss;
  oss << "label_" << debug_name << ":";
  return oss.str();
}
} // namespace cafe
