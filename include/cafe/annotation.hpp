#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apidef.hpp"
#include "label.hpp"
#include "value.hpp"

namespace cafe {


class element_value;

class CAFE_API annotation {
public:
  std::string desc;
  annotation() = default;
  explicit annotation(const std::string_view& desc);
  ~annotation() = default;
  annotation(const annotation&) = default;
  annotation(annotation&&) = default;
  annotation& operator=(const annotation&) = default;
  annotation& operator=(annotation&&) = default;
  std::vector<std::pair<std::string, element_value>> values;
  std::string to_string() const;
};

using element_type = std::variant<int8_t, uint16_t, double, float, int32_t, int64_t, int16_t, bool, std::string,
                            std::pair<std::string, std::string>, type, annotation, std::vector<element_value>>;
class CAFE_API element_value {
public:

  element_value() = default;
  element_value(element_type value);
  ~element_value() = default;
  element_value(const element_value&) = default;
  element_value(element_value&&) = default;
  element_value& operator=(const element_value&) = default;
  element_value& operator=(element_value&&) = default;
  element_type value;
  std::string to_string() const;
};


namespace target {
class CAFE_API type_parameter {
public:
  uint8_t index{};
  explicit type_parameter(uint8_t index);
  ~type_parameter() = default;
  type_parameter(const type_parameter&) = default;
  type_parameter(type_parameter&&) = default;
  type_parameter& operator=(const type_parameter&) = default;
  type_parameter& operator=(type_parameter&&) = default;
};

class CAFE_API supertype {
public:
  uint16_t index{};
  explicit supertype(uint16_t index);
  ~supertype() = default;
  supertype(const supertype&) = default;
  supertype(supertype&&) = default;
  supertype& operator=(const supertype&) = default;
  supertype& operator=(supertype&&) = default;
  bool is_super() const;
  void make_super();
};

class CAFE_API type_parameter_bound {
public:
  uint8_t type_parameter_index{};
  uint8_t bound_index{};
  type_parameter_bound(uint8_t type_parameter_index, uint8_t bound_index);
  ~type_parameter_bound() = default;
  type_parameter_bound(const type_parameter_bound&) = default;
  type_parameter_bound(type_parameter_bound&&) = default;
  type_parameter_bound& operator=(const type_parameter_bound&) = default;
  type_parameter_bound& operator=(type_parameter_bound&&) = default;
};

class CAFE_API empty {
public:
  empty() = default;
};

class CAFE_API formal_parameter {
public:
  uint8_t index{};
  explicit formal_parameter(uint8_t index);
  ~formal_parameter() = default;
  formal_parameter(const formal_parameter&) = default;
  formal_parameter(formal_parameter&&) = default;
  formal_parameter& operator=(const formal_parameter&) = default;
  formal_parameter& operator=(formal_parameter&&) = default;
};

class CAFE_API throws {
public:
  uint16_t index{};
  explicit throws(uint16_t index);
  ~throws() = default;
  throws(const throws&) = default;
  throws(throws&&) = default;
  throws& operator=(const throws&) = default;
  throws& operator=(throws&&) = default;
};
class CAFE_API local {
public:
  label start;
  label end;
  uint16_t index{};
  local(label start, label end, uint16_t index);
  ~local() = default;
  local(const local&) = default;
  local(local&&) = default;
  local& operator=(const local&) = default;
  local& operator=(local&&) = default;
};
class CAFE_API localvar {
public:
  std::vector<local> table;
  explicit localvar(const std::vector<local>& table);
  ~localvar() = default;
  localvar(const localvar&) = default;
  localvar(localvar&&) = default;
  localvar& operator=(const localvar&) = default;
  localvar& operator=(localvar&&) = default;
};
class CAFE_API catch_target {
public:
  uint16_t index{};
  explicit catch_target(uint16_t index);
  ~catch_target() = default;
  catch_target(const catch_target&) = default;
  catch_target(catch_target&&) = default;
  catch_target& operator=(const catch_target&) = default;
  catch_target& operator=(catch_target&&) = default;
};
class CAFE_API offset_target {
public:
  label offset;
  explicit offset_target(label offset);
  ~offset_target() = default;
  offset_target(const offset_target&) = default;
  offset_target(offset_target&&) = default;
  offset_target& operator=(const offset_target&) = default;
  offset_target& operator=(offset_target&&) = default;
};
class CAFE_API type_argument {
public:
  label offset;
  uint8_t index{};
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
CAFE_API std::string to_string(const type_annotation_target& target);
class CAFE_API type_path {
public:
  std::vector<std::pair<uint8_t, uint8_t>> path;
  type_path() = default;
  explicit type_path(const std::vector<std::pair<uint8_t, uint8_t>>& path);
  ~type_path() = default;
  type_path(const type_path&) = default;
  type_path(type_path&&) = default;
  type_path& operator=(const type_path&) = default;
  type_path& operator=(type_path&&) = default;
  std::string to_string() const;
};

class CAFE_API type_annotation {
public:
  uint8_t target_type{};
  type_annotation_target target_info;
  type_path target_path;
  std::string desc;
  std::vector<std::pair<std::string, element_value>> values;
  type_annotation();
  type_annotation(uint8_t target_type, type_annotation_target target_info, type_path target_path,
                  const std::string_view& desc);
  ~type_annotation() = default;
  type_annotation(const type_annotation&) = default;
  type_annotation(type_annotation&&) = default;
  type_annotation& operator=(const type_annotation&) = default;
  type_annotation& operator=(type_annotation&&) = default;
  std::string to_string() const;
};
} // namespace cafe
