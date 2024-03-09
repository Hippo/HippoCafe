#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "attribute.hpp"
#include "constant_pool.hpp"
#include "instruction.hpp"

namespace cafe {
class method_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class field_info {
public:
  uint16_t access_flags;
  uint16_t name_index;
  uint16_t descriptor_index;
  std::vector<attribute::attribute> attributes;
};

class class_file {
public:
  uint32_t magic;
  uint16_t minor_version;
  uint16_t major_version;
  cp::constant_pool constant_pool;
  uint16_t access_flags;
  uint16_t this_class;
  uint16_t super_class;
  std::vector<uint16_t> interfaces;
  std::vector<field_info> fields;
  std::vector<method_info> methods;
  std::vector<attribute::attribute> attributes;
};

std::istream& operator>>(std::istream& stream, class_file& file);

std::ostream& operator<<(std::ostream& stream, const class_file& file);

class field_model {
public:
  uint16_t access_flags{};
  std::string name;
  std::string descriptor;
  field_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor);
  ~field_model() = default;
  field_model(const field_model&) = default;
  field_model(field_model&&) = default;
  field_model& operator=(const field_model&) = default;
  field_model& operator=(field_model&&) = default;
  bool synthetic = false;
  bool deprecated = false;
  std::optional<value> constant_value;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  [[nodiscard]] std::string to_string() const;
};

class method_model {
public:
  uint16_t access_flags{};
  std::string name;
  std::string descriptor;
  method_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor);
  ~method_model() = default;
  method_model(const method_model&) = default;
  method_model(method_model&&) = default;
  method_model& operator=(const method_model&) = default;
  method_model& operator=(method_model&&) = default;
  code code;
  bool synthetic = false;
  bool deprecated = false;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<std::vector<annotation>> visible_parameter_annotations;
  std::vector<std::vector<annotation>> invisible_parameter_annotations;
  std::optional<element_value> annotation_default;
  std::vector<std::pair<uint16_t, std::string>> method_parameters;
  std::vector<std::string> exceptions;
  [[nodiscard]] std::string to_string() const;
};

class inner_class {
public:
  std::string name;
  std::string outer_name;
  std::string inner_name;
  uint16_t access_flags{};
  inner_class(const std::string_view& name, const std::string_view& outer_name, const std::string_view& inner_name,
              uint16_t access_flags);
  ~inner_class() = default;
  inner_class(const inner_class&) = default;
  inner_class(inner_class&&) = default;
  inner_class& operator=(const inner_class&) = default;
  inner_class& operator=(inner_class&&) = default;
 };

class requires_model {
public:
  std::string name;
  uint16_t access_flags{};
  std::string version;
  requires_model(const std::string_view& name, uint16_t access_flags, const std::string_view& version);
  ~requires_model() = default;
  requires_model(const requires_model&) = default;
  requires_model(requires_model&&) = default;
  requires_model& operator=(const requires_model&) = default;
  requires_model& operator=(requires_model&&) = default;
};

class exports_model {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  exports_model(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~exports_model() = default;
  exports_model(const exports_model&) = default;
  exports_model(exports_model&&) = default;
  exports_model& operator=(const exports_model&) = default;
  exports_model& operator=(exports_model&&) = default;
};

class opens_model {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  opens_model(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~opens_model() = default;
  opens_model(const opens_model&) = default;
  opens_model(opens_model&&) = default;
  opens_model& operator=(const opens_model&) = default;
  opens_model& operator=(opens_model&&) = default;
};

class provides_model {
public:
  std::string service;
  std::vector<std::string> providers;
  provides_model(const std::string_view& service, const std::vector<std::string>& providers);
  ~provides_model() = default;
  provides_model(const provides_model&) = default;
  provides_model(provides_model&&) = default;
  provides_model& operator=(const provides_model&) = default;
  provides_model& operator=(provides_model&&) = default;
};

class module_model {
public:
  std::string name;
  uint16_t access_flags{};
  std::string version;
  module_model(const std::string_view& name, uint16_t access_flags, const std::string_view& version);
  ~module_model() = default;
  module_model(const module_model&) = default;
  module_model(module_model&&) = default;
  module_model& operator=(const module_model&) = default;
  module_model& operator=(module_model&&) = default;
  std::vector<requires_model>
    requires;
  std::vector<exports_model> exports;
  std::vector<opens_model> opens;
  std::vector<std::string> uses;
  std::vector<provides_model> provides;
};

class record_component {
public:
  std::string name;
  std::string descriptor;
  record_component(const std::string_view& name, const std::string_view& descriptor);
  ~record_component() = default;
  record_component(const record_component&) = default;
  record_component(record_component&&) = default;
  record_component& operator=(const record_component&) = default;
  record_component& operator=(record_component&&) = default;
  std::string signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
};

class class_model {
public:
  uint32_t version{};
  uint16_t access_flags{};
  std::string name;
  std::string super_name;
  class_model(uint32_t version, uint16_t access_flags, const std::string_view& name,
              const std::string_view& super_name = "java/lang/Object");
  class_model() = default;
  ~class_model() = default;
  class_model(const class_model&) = default;
  class_model(class_model&&) = default;
  class_model& operator=(const class_model&) = default;
  class_model& operator=(class_model&&) = default;
  std::vector<std::string> interfaces;
  std::vector<field_model> fields;
  std::vector<method_model> methods;
  bool synthetic = false;
  bool deprecated = false;
  std::string signature;
  std::string source_file;
  std::vector<inner_class> inner_classes;
  std::string enclosing_owner;
  std::string enclosing_name;
  std::string enclosing_descriptor;
  std::string source_debug_extension;
  std::optional<module_model> module;
  std::vector<std::string> module_packages;
  std::string module_main_class;
  std::string nest_host;
  std::vector<std::string> nest_members;
  std::vector<std::string> permitted_subclasses;
  std::vector<record_component> record_components;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  [[nodiscard]] std::string to_string() const;
};

std::istream& operator>>(std::istream& stream, class_model& model);

} // namespace cafe
