#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "apidef.hpp"

namespace cafe {
class CAFE_API label {
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
  [[nodiscard]] uint64_t id() const;
  [[nodiscard]] std::string to_string() const;

private:
  uint64_t id_;
};
} // namespace cafe
