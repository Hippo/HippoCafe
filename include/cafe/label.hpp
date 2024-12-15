#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "apidef.hpp"

namespace cafe {

using label_id = uint64_t;

class CAFE_API label {
public:
  std::string debug_name;
  label();
  explicit label(const std::string_view& debug_name);
  label(label_id id, const std::string_view& debug_name);
  explicit label(label_id id);
  ~label() = default;
  label(const label& other);
  label(label&&) = default;
  label& operator=(const label&) = default;
  label& operator=(label&&) = default;
  bool operator==(const label& other) const;
  bool operator!=(const label& other) const;
  label_id id() const;
  std::string to_string() const;

private:
  label_id id_;

  static label_id next_id();
};
} // namespace cafe
