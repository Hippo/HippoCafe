#include "cafe/label.hpp"

#include <random>
#include <sstream>
#include <atomic>

namespace cafe {

label::label() : id_(next_id()) {
}
label::label(const std::string_view& debug_name) : debug_name(debug_name), id_(next_id()) {
}
label::label(label_id id, const std::string_view& debug_name) : debug_name(debug_name), id_(id) {
}
label::label(label_id id) : id_(id) {
}
bool label::operator==(const label& other) const {
  return id_ == other.id_;
}
bool label::operator!=(const label& other) const {
  return id_ != other.id_;
}
label_id label::id() const {
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

label_id label::next_id() {
  static std::atomic_uint64_t id{0};
  return id++;
}
} // namespace cafe
