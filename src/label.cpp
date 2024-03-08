#include "cafe/label.hpp"

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
}
} // namespace cafe
