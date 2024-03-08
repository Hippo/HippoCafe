#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "label.hpp"
#include "value.hpp"

namespace cafe {


class element_value;

class annotation {
public:
  std::string descriptor;
  annotation() = default;
  explicit annotation(const std::string_view& descriptor);
  ~annotation() = default;
  annotation(const annotation&) = default;
  annotation(annotation&&) = default;
  annotation& operator=(const annotation&) = default;
  annotation& operator=(annotation&&) = default;
  std::unordered_map<std::string, element_value> values;
  [[nodiscard]] std::string to_string() const;
};

class element_value {
public:
  using type = std::variant<int8_t, uint16_t, double, float, int32_t, int64_t, int16_t, bool, std::string,
                            std::pair<std::string, std::string>, class_value, annotation, std::vector<element_value>>;
  type value;
  [[nodiscard]] std::string to_string() const;
};


namespace target {
class type_parameter {
public:
  uint8_t index;
  explicit type_parameter(uint8_t index);
  ~type_parameter() = default;
  type_parameter(const type_parameter&) = default;
  type_parameter(type_parameter&&) = default;
  type_parameter& operator=(const type_parameter&) = default;
  type_parameter& operator=(type_parameter&&) = default;
};

class supertype {
public:
  std::string name;
  explicit supertype(const std::string_view& name);
  ~supertype() = default;
  supertype(const supertype&) = default;
  supertype(supertype&&) = default;
  supertype& operator=(const supertype&) = default;
  supertype& operator=(supertype&&) = default;
};

class type_parameter_bound {
public:
  uint8_t type_parameter_index;
  uint8_t bound_index;
  type_parameter_bound(uint8_t type_parameter_index, uint8_t bound_index);
  ~type_parameter_bound() = default;
  type_parameter_bound(const type_parameter_bound&) = default;
  type_parameter_bound(type_parameter_bound&&) = default;
  type_parameter_bound& operator=(const type_parameter_bound&) = default;
  type_parameter_bound& operator=(type_parameter_bound&&) = default;
};

class empty {
public:
  empty() = default;
};

class formal_parameter {
public:
  uint8_t index;
  explicit formal_parameter(uint8_t index);
  ~formal_parameter() = default;
  formal_parameter(const formal_parameter&) = default;
  formal_parameter(formal_parameter&&) = default;
  formal_parameter& operator=(const formal_parameter&) = default;
  formal_parameter& operator=(formal_parameter&&) = default;
};

class throws {
public:
  std::string name;
  explicit throws(const std::string_view& name);
  ~throws() = default;
  throws(const throws&) = default;
  throws(throws&&) = default;
  throws& operator=(const throws&) = default;
  throws& operator=(throws&&) = default;
};
class local {
public:
  label start;
  label end;
  uint16_t index;
  local(label start, label end, uint16_t index);
  ~local() = default;
  local(const local&) = default;
  local(local&&) = default;
  local& operator=(const local&) = default;
  local& operator=(local&&) = default;
};
class localvar {
public:
  std::vector<local> table;
  explicit localvar(const std::vector<local>& table);
  ~localvar() = default;
  localvar(const localvar&) = default;
  localvar(localvar&&) = default;
  localvar& operator=(const localvar&) = default;
  localvar& operator=(localvar&&) = default;
};
class catch_target {
public:
  label handler_label;
  explicit catch_target(label handler_label);
  ~catch_target() = default;
  catch_target(const catch_target&) = default;
  catch_target(catch_target&&) = default;
  catch_target& operator=(const catch_target&) = default;
  catch_target& operator=(catch_target&&) = default;
};
class offset_target {
public:
  label offset;
  explicit offset_target(label offset);
  ~offset_target() = default;
  offset_target(const offset_target&) = default;
  offset_target(offset_target&&) = default;
  offset_target& operator=(const offset_target&) = default;
  offset_target& operator=(offset_target&&) = default;
};
class type_argument {
public:
  label offset;
  uint8_t index;
  type_argument(label offset, uint8_t index);
  ~type_argument() = default;
  type_argument(const type_argument&) = default;
  type_argument(type_argument&&) = default;
  type_argument& operator=(const type_argument&) = default;
  type_argument& operator=(type_argument&&) = default;
};
} // namespace target

using type_annotation_target = std::variant<target::type_parameter, target::supertype, target::type_parameter_bound,
                                            target::empty, target::formal_parameter, target::throws, target::localvar,
                                            target::catch_target, target::offset_target, target::type_argument>;
std::string to_string(const type_annotation_target& target);
class type_path {
public:
  std::vector<std::pair<uint8_t, uint8_t>> path;
  type_path() = default;
  explicit type_path(const std::vector<std::pair<uint8_t, uint8_t>>& path);
  ~type_path() = default;
  type_path(const type_path&) = default;
  type_path(type_path&&) = default;
  type_path& operator=(const type_path&) = default;
  type_path& operator=(type_path&&) = default;
  [[nodiscard]] std::string to_string() const;
};

class type_annotation {
public:
  uint8_t target_type{};
  type_annotation_target target_info;
  type_path target_path;
  std::string descriptor;
  std::unordered_map<std::string, element_value> values;
  type_annotation();
  type_annotation(uint8_t target_type, type_annotation_target target_info, type_path target_path,
                  const std::string_view& descriptor);
  ~type_annotation() = default;
  type_annotation(const type_annotation&) = default;
  type_annotation(type_annotation&&) = default;
  type_annotation& operator=(const type_annotation&) = default;
  type_annotation& operator=(type_annotation&&) = default;
  [[nodiscard]] std::string to_string() const;
};

namespace attribute {
class element_value;
struct element_pair;

class annotation {
public:
  uint16_t type_index;
  std::vector<element_pair> elements;
};

class element_value {
public:
  uint8_t tag{};

  struct enum_value {
    uint16_t type_name_index;
    uint16_t const_name_index;
  };

  std::variant<uint16_t, enum_value, annotation, std::vector<element_value>> value;
};

struct element_pair {
  uint16_t name_index{};
  element_value value;
};

class type_parameter {
public:
  uint8_t index;
};

class supertype {
public:
  uint16_t index;
};

class type_parameter_bound {
public:
  uint8_t type_parameter_index;
  uint8_t bound_index;
};

class empty {};

class formal_parameter {
public:
  uint8_t index;
};

class throws {
public:
  uint16_t index;
};

class localvar {
public:
  struct local {
    uint16_t start_pc;
    uint16_t length;
    uint16_t index;
  };

  std::vector<local> table;
};

class catch_target {
public:
  uint16_t index;
};

class offset_target {
public:
  uint16_t offset;
};

class type_argument {
public:
  uint16_t offset;
  uint8_t index;
};

using type_annotation_target = std::variant<type_parameter, supertype, type_parameter_bound, empty, formal_parameter,
                                            throws, localvar, catch_target, offset_target, type_argument>;

class type_path {
public:
  struct path {
    uint8_t kind;
    uint8_t index;
  };

  std::vector<path> paths;
};

class type_annotation {
public:
  uint8_t target_type;
  type_annotation_target target_info;
  type_path target_path;
  uint16_t type_index;
  std::vector<element_pair> elements;
};
} // namespace attribute
} // namespace cafe
