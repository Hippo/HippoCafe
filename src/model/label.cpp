#include "cafe/model/label.hpp"

#include <random>
#include <sstream>

namespace cafe {

static uint64_t new_id() {
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<uint64_t> distribution(0, std::numeric_limits<uint64_t>::max());
  return distribution(generator);
}

label::label() : id_(new_id()) {
}
label::label(const std::string_view& debug_name) : debug_name(debug_name), id_(reinterpret_cast<uintptr_t>(this)) {
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
