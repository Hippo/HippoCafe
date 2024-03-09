#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "annotation.hpp"
#include "stackmap.hpp"

namespace cafe::attribute {
enum class attribute_type : uint8_t {
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

class unknown {
public:
  uint16_t name_index;
  std::vector<uint8_t> info;
};

class constant_value {
public:
  uint16_t index;
};


class stack_map_table {
public:
  std::vector<stack_map_frame> entries;
};

class exceptions {
public:
  std::vector<uint16_t> exception_index_table;
};

class inner_classes {
public:
  struct inner_class {
    uint16_t inner_index;
    uint16_t outer_index;
    uint16_t name_index;
    uint16_t access_flags;
  };

  std::vector<inner_class> classes;
};

class enclosing_method {
public:
  uint16_t class_index;
  uint16_t method_index;
};

class synthetic {};

class signature {
public:
  uint16_t index;
};

class source_file {
public:
  uint16_t index;
};

class source_debug_extension {
public:
  std::vector<uint8_t> debug_extension;
};

class line_number_table {
public:
  struct line_number {
    uint16_t start_pc;
    uint16_t number;
  };

  std::vector<line_number> line_numbers;
};

class local_variable_table {
public:
  struct local_variable {
    uint16_t start_pc;
    uint16_t length;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t index;
  };

  std::vector<local_variable> local_variables;
};

class local_variable_type_table {
public:
  struct local_variable {
    uint16_t start_pc;
    uint16_t length;
    uint16_t name_index;
    uint16_t signature_index;
    uint16_t index;
  };

  std::vector<local_variable> local_variables;
};

class deprecated {};

class runtime_visible_annotations {
public:
  std::vector<annotation> annotations;
};

class runtime_invisible_annotations {
public:
  std::vector<annotation> annotations;
};

class runtime_visible_parameter_annotations {
public:
  std::vector<std::vector<annotation>> parameter_annotations;
};

class runtime_invisible_parameter_annotations {
public:
  std::vector<std::vector<annotation>> parameter_annotations;
};

class runtime_visible_type_annotations {
public:
  std::vector<type_annotation> annotations;
};

class runtime_invisible_type_annotations {
public:
  std::vector<type_annotation> annotations;
};

class annotation_default {
public:
  element_value default_value;
};

class bootstrap_methods {
public:
  struct bootstrap_method {
    uint16_t index;
    std::vector<uint16_t> arguments;
  };

  std::vector<bootstrap_method> methods;
};

class method_parameters {
public:
  struct parameter {
    uint16_t name_index;
    uint16_t access_flags;
  };

  std::vector<parameter> parameters;
};

class module {
public:
  uint16_t module_name_index;
  uint16_t module_flags;
  uint16_t module_version_index;

  struct require {
    uint16_t require_index;
    uint16_t require_flags;
    uint16_t require_version_index;
  };

  std::vector<require>
    requires;

  struct mod_export {
    uint16_t exports_index;
    uint16_t exports_flags;
    std::vector<uint16_t> exports_to_index;
  };

  std::vector<mod_export> exports;

  struct open {
    uint16_t opens_index;
    uint16_t opens_flags;
    std::vector<uint16_t> opens_to_index;
  };

  std::vector<open> opens;

  std::vector<uint16_t> uses;

  struct provide {
    uint16_t provides_index;
    std::vector<uint16_t> provides_with_index;
  };

  std::vector<provide> provides;
};

class module_packages {
public:
  std::vector<uint16_t> package_indices;
};

class module_main_class {
public:
  uint16_t index;
};

class nest_host {
public:
  uint16_t index;
};

class nest_members {
public:
  std::vector<uint16_t> classes;
};


class permitted_subclasses {
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

class code {
public:
  uint16_t max_stack;
  uint16_t max_locals;
  std::vector<uint8_t> bytecode;

  struct exception {
    uint16_t start_pc;
    uint16_t end_pc;
    uint16_t handler_pc;
    uint16_t catch_type;
  };

  std::vector<exception> exceptions;
  std::vector<attribute> attributes;
};

class record {
public:
  struct component {
    uint16_t name_index;
    uint16_t descriptor_index;
    std::vector<attribute> attributes;
  };

  std::vector<component> components;
};
} // namespace cafe::attribute
