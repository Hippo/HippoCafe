#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cafe {
class label {
public:
  std::string debug_name;
  label();
  explicit label(const std::string_view& debug_name);
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
