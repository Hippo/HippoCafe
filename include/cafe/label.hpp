#pragma once

#include <cstdint>
#include <string>

namespace cafe {
class label {
public:
  std::string debug_name;
  label();
  ~label() = default;
  label(const label& other);
  label(label&&) = default;
  label& operator=(const label&) = default;
  label& operator=(label&&) = default;
  bool operator==(const label& other) const;
  [[nodiscard]] uintptr_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uintptr_t id_;
};
} // namespace cafe
