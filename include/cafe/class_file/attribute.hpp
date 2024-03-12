#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "annotation.hpp"
#include "cafe/apidef.hpp"
#include "stackmap.hpp"

namespace cafe::attribute {
enum class CAFE_API attribute_type : uint8_t {
  constant_value,
  code,
  stack_map_table,
  exceptions,
  inner_classes,
  enclosing_method,
  synthetic,
  signature,
  source_file,
  source_debug_extension,
  line_number_table,
  local_variable_table,
  local_variable_type_table,
  deprecated,
  runtime_visible_annotations,
  runtime_invisible_annotations,
  runtime_visible_parameter_annotations,
  runtime_invisible_parameter_annotations,
  runtime_visible_type_annotations,
  runtime_invisible_type_annotations,
  annotation_default,
  bootstrap_methods,
  method_parameters,
  module,
  module_package,
  module_main_class,
  nest_host,
  nest_members,
  record,
  permitted_subclasses
};

class CAFE_API unknown {
public:
  uint16_t name_index;
  std::vector<uint8_t> info;
};

class CAFE_API constant_value {
public:
  uint16_t index;
};


class CAFE_API stack_map_table {
public:
  std::vector<stack_map_frame> entries;
};

class CAFE_API exceptions {
public:
  std::vector<uint16_t> exception_index_table;
};

class CAFE_API inner_classes {
public:
  struct CAFE_API inner_class {
    uint16_t inner_index;
    uint16_t outer_index;
    uint16_t name_index;
    uint16_t access_flags;
  };

  std::vector<inner_class> classes;
};

class CAFE_API enclosing_method {
public:
  uint16_t class_index;
  uint16_t method_index;
};

class CAFE_API synthetic {};

class CAFE_API signature {
public:
  uint16_t index;
};

class CAFE_API source_file {
public:
  uint16_t index;
};

class CAFE_API source_debug_extension {
public:
  std::vector<uint8_t> debug_extension;
};

class CAFE_API line_number_table {
public:
  struct CAFE_API line_number {
    uint16_t start_pc;
    uint16_t number;
  };

  std::vector<line_number> line_numbers;
};

class CAFE_API local_variable_table {
public:
  struct CAFE_API local_variable {
    uint16_t start_pc;
    uint16_t length;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t index;
  };

  std::vector<local_variable> local_variables;
};

class CAFE_API local_variable_type_table {
public:
  struct CAFE_API local_variable {
    uint16_t start_pc;
    uint16_t length;
    uint16_t name_index;
    uint16_t signature_index;
    uint16_t index;
  };

  std::vector<local_variable> local_variables;
};

class CAFE_API deprecated {};

class CAFE_API runtime_visible_annotations {
public:
  std::vector<annotation> annotations;
};

class CAFE_API runtime_invisible_annotations {
public:
  std::vector<annotation> annotations;
};

class CAFE_API runtime_visible_parameter_annotations {
public:
  std::vector<std::vector<annotation>> parameter_annotations;
};

class CAFE_API runtime_invisible_parameter_annotations {
public:
  std::vector<std::vector<annotation>> parameter_annotations;
};

class CAFE_API runtime_visible_type_annotations {
public:
  std::vector<type_annotation> annotations;
};

class CAFE_API runtime_invisible_type_annotations {
public:
  std::vector<type_annotation> annotations;
};

class CAFE_API annotation_default {
public:
  element_value default_value;
};

class CAFE_API bootstrap_methods {
public:
  struct CAFE_API bootstrap_method {
    uint16_t index;
    std::vector<uint16_t> arguments;
  };

  std::vector<bootstrap_method> methods;
};

class CAFE_API method_parameters {
public:
  struct CAFE_API parameter {
    uint16_t name_index;
    uint16_t access_flags;
  };

  std::vector<parameter> parameters;
};

class CAFE_API module {
public:
  uint16_t module_name_index{};
  uint16_t module_flags{};
  uint16_t module_version_index{};

  struct CAFE_API mod_require {
    uint16_t require_index;
    uint16_t require_flags;
    uint16_t require_version_index;
  };

  std::vector<mod_require> mod_requires;

  struct CAFE_API mod_export {
    uint16_t exports_index{};
    uint16_t exports_flags{};
    std::vector<uint16_t> exports_to_index;
  };

  std::vector<mod_export> exports;

  struct CAFE_API open {
    uint16_t opens_index{};
    uint16_t opens_flags{};
    std::vector<uint16_t> opens_to_index;
  };

  std::vector<open> opens;

  std::vector<uint16_t> uses;

  struct CAFE_API provide {
    uint16_t provides_index{};
    std::vector<uint16_t> provides_with_index;
  };

  std::vector<provide> provides;
};

class CAFE_API module_packages {
public:
  std::vector<uint16_t> package_indices;
};

class CAFE_API module_main_class {
public:
  uint16_t index;
};

class CAFE_API nest_host {
public:
  uint16_t index;
};

class CAFE_API nest_members {
public:
  std::vector<uint16_t> classes;
};


class CAFE_API permitted_subclasses {
public:
  std::vector<uint16_t> classes;
};


class code;
class record;

using attribute =
    std::variant<unknown, constant_value, code, stack_map_table, exceptions, inner_classes, enclosing_method, synthetic,
                 signature, source_file, source_debug_extension, line_number_table, local_variable_table,
                 local_variable_type_table, deprecated, runtime_visible_annotations, runtime_invisible_annotations,
                 runtime_visible_parameter_annotations, runtime_invisible_parameter_annotations,
                 runtime_visible_type_annotations, runtime_invisible_type_annotations, annotation_default,
                 bootstrap_methods, method_parameters, module, module_packages, module_main_class, nest_host,
                 nest_members, record, permitted_subclasses>;

class CAFE_API code {
public:
  uint16_t max_stack{};
  uint16_t max_locals{};
  std::vector<uint8_t> bytecode;

  struct CAFE_API exception {
    uint16_t start_pc{};
    uint16_t end_pc{};
    uint16_t handler_pc{};
    uint16_t catch_type{};
  };

  std::vector<exception> exceptions;
  std::vector<attribute> attributes;
};

class CAFE_API record {
public:
  struct CAFE_API component {
    uint16_t name_index{};
    uint16_t descriptor_index{};
    std::vector<attribute> attributes;
  };

  std::vector<component> components;
};
} // namespace cafe::attribute
