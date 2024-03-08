#pragma once

#include <cstdint>

namespace cafe {
class label {
public:
  label();
  ~label() = default;
  label(const label& other);
  label(label&&) = default;
  label& operator=(const label&) = default;
  label& operator=(label&&) = default;
  bool operator==(const label& other) const;
  [[nodiscard]] uintptr_t id() const;

private:
  uintptr_t id_;
};
} // namespace cafe
