#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <list>

#include "apidef.hpp"
#include "annotation.hpp"
#include "instruction.hpp"

namespace cafe {

class CAFE_API attribute {
public:
  std::string name;
  std::vector<int8_t> data;
  attribute() = default;
  attribute(const std::string_view& name, const std::vector<int8_t>& data);
  ~attribute() = default;
  attribute(const attribute& other) = default;
  attribute(attribute&& other) noexcept = default;
  attribute& operator=(const attribute& other) = default;
  attribute& operator=(attribute&& other) noexcept = default;
};

class CAFE_API record_component {
public:
  std::string name;
  std::string desc;
  std::optional<std::string> signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  record_component() = default;
  record_component(const std::string_view& name, const std::string_view& desc);
  record_component(const record_component& other) = default;
  record_component(record_component&& other) noexcept = default;
  record_component& operator=(const record_component& other) = default;
  record_component& operator=(record_component&& other) noexcept = default;
  ~record_component() = default;
};

class CAFE_API module_require {
public:
  std::string module;
  uint16_t access_flags{};
  std::optional<std::string> version;
  module_require() = default;
  module_require(const std::string_view& module, uint16_t access_flags, const std::optional<std::string>& version);
  ~module_require() = default;
  module_require(const module_require& other) = default;
  module_require(module_require&& other) noexcept = default;
  module_require& operator=(const module_require& other) = default;
  module_require& operator=(module_require&& other) noexcept = default;
};

class CAFE_API module_export {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  module_export() = default;
  module_export(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~module_export() = default;
  module_export(const module_export& other) = default;
  module_export(module_export&& other) noexcept = default;
  module_export& operator=(const module_export& other) = default;
  module_export& operator=(module_export&& other) noexcept = default;
};

class CAFE_API module_open {
public:
  std::string package;
  uint16_t access_flags{};
  std::vector<std::string> modules;
  module_open() = default;
  module_open(const std::string_view& package, uint16_t access_flags, const std::vector<std::string>& modules);
  ~module_open() = default;
  module_open(const module_open& other) = default;
  module_open(module_open&& other) noexcept = default;
  module_open& operator=(const module_open& other) = default;
  module_open& operator=(module_open&& other) noexcept = default;
};

class CAFE_API module_provide {
public:
  std::string service;
  std::vector<std::string> providers;
  module_provide() = default;
  module_provide(const std::string_view& service, const std::vector<std::string>& providers);
  ~module_provide() = default;
  module_provide(const module_provide& other) = default;
  module_provide(module_provide&& other) noexcept = default;
  module_provide& operator=(const module_provide& other) = default;
  module_provide& operator=(module_provide&& other) noexcept = default;
};

class CAFE_API module {
public:
  std::string name;
  uint16_t access_flags{};
  std::optional<std::string> version;
  std::vector<module_require>
    mod_requires;
  std::vector<module_export> exports;
  std::vector<module_open> opens;
  std::vector<std::string> uses;
  std::vector<module_provide> provides;
  module() = default;
  module(const std::string_view& name, uint16_t access_flags, const std::optional<std::string>& version);
  ~module() = default;
  module(const module& other) = default;
  module(module&& other) noexcept = default;
  module& operator=(const module& other) = default;
  module& operator=(module&& other) noexcept = default;
};

class CAFE_API code : public std::list<instruction> {
public:
  uint16_t max_stack{};
  uint16_t max_locals{};
  std::vector<std::pair<uint16_t, label>> line_numbers;
  std::vector<tcb> tcbs;
  std::vector<local_var> locals;
  std::vector<std::pair<label, frame>> frames;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  code() = default;
  explicit code(const std::allocator<instruction>& allocator);
  explicit code(size_type count);
  code(size_type count,
       const std::allocator<instruction>& allocator);
  code(size_type count, const instruction& value);
  code(const list& insns);
  code(const list& insns, const std::_Identity_t<std::allocator<instruction>>& allocator);
  code(list&& insns);
  code(list&& insns, const std::_Identity_t<std::allocator<instruction>>& allocator);
  code(const std::initializer_list<instruction>& insns);
  code(const std::initializer_list<instruction>& insns, const std::allocator<instruction>& allocator);
  code(uint16_t max_stack, uint16_t max_locals);
  ~code() = default;
  code(const code& other) = default;
  code(code&& other) noexcept = default;
  code& operator=(const code& other) = default;
  code& operator=(code&& other) noexcept = default;

  void add_label(const label& label);
  void add_insn(uint8_t opcode);
  void add_var_insn(uint8_t opcode, uint16_t index);
  void add_type_insn(uint8_t opcode, const std::string_view& type);
  void add_field_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                      const std::string_view& descriptor);
  void add_method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                      const std::string_view& descriptor);
  void add_method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                      const std::string_view& descriptor, bool interface);
  void add_iinc_insn(uint16_t index, int16_t value);
  void add_push_insn(value value);
  void add_branch_insn(uint8_t opcode, label target);
  void add_lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets);
  void add_table_switch_insn(label default_target, int32_t low, int32_t high,
                               const std::vector<label>& targets);
  void add_multi_array_insn(const std::string_view& descriptor, uint8_t dims);
  void add_array_insn(const std::variant<uint8_t, std::string>& type);
  void add_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor, method_handle handle,
                                 const std::vector<value>& args);

  const_iterator find_label(const label& lbl) const;
  iterator find_label(const label& lbl);

  std::string to_string() const;
  std::string to_string(const std::string_view& delim) const;
};

class CAFE_API field {
public:
  uint16_t access_flags{};
  std::string name;
  std::string desc;
  std::optional<value> constant_value;
  bool synthetic = false;
  bool deprecated = false;
  std::optional<std::string> signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  field() = default;
  field(uint16_t access_flags, const std::string_view& name, const std::string_view& desc);
  ~field() = default;
  field(const field& other) = default;
  field(field&& other) noexcept = default;
  field& operator=(const field& other) = default;
  field& operator=(field&& other) noexcept = default;
};

class CAFE_API method {
public:
  uint16_t access_flags{};
  std::string name;
  std::string desc;
  code code;
  std::vector<std::string> exceptions;
  std::vector<std::vector<annotation>> visible_parameter_annotations;
  std::vector<std::vector<annotation>> invisible_parameter_annotations;
  std::optional<element_value> annotation_default;
  std::vector<std::pair<uint16_t, std::optional<std::string>>> parameters;
  bool synthetic = false;
  bool deprecated = false;
  std::optional<std::string> signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  method() = default;
  method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc);
  method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc, cafe::code&& code);
  method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc, const cafe::code& code);
  ~method() = default;
  method(const method& other) = default;
  method(method&& other) noexcept = default;
  method& operator=(const method& other) = default;
  method& operator=(method&& other) noexcept = default;

  std::string name_desc(const std::string_view& separator) const;
  std::string name_desc() const;
};

class CAFE_API inner_class {
public:
  std::string name;
  std::optional<std::string> outer_name;
  std::optional<std::string> inner_name;
  uint16_t access_flags{};
  inner_class() = default;
  inner_class(const std::string_view& name, const std::optional<std::string>& outer_name, const std::optional<std::string>& inner_name,
              uint16_t access_flags);
  ~inner_class() = default;
  inner_class(const inner_class& other) = default;
  inner_class(inner_class&& other) noexcept = default;
  inner_class& operator=(const inner_class& other) = default;
  inner_class& operator=(inner_class&& other) noexcept = default;
};

class CAFE_API class_file {
public:
  uint32_t version{};
  uint16_t access_flags{};
  std::string name;
  std::optional<std::string> super_name;
  std::vector<std::string> interfaces;
  std::vector<field> fields;
  std::vector<method> methods;
  std::optional<std::string> source_file;
  std::vector<inner_class> inner_classes;
  std::optional<std::pair<std::string, std::optional<std::pair<std::string, std::string>>>> enclosing_method;
  std::optional<std::string> source_debug_extension;
  std::optional<module> module;
  std::vector<std::string> module_packages;
  std::optional<std::string> module_main_class;
  std::optional<std::string> nest_host;
  std::vector<std::string> nest_members;
  std::vector<record_component> record_components;
  std::vector<std::string> permitted_subclasses;
  bool synthetic = false;
  bool deprecated = false;
  std::optional<std::string> signature;
  std::vector<annotation> visible_annotations;
  std::vector<annotation> invisible_annotations;
  std::vector<type_annotation> visible_type_annotations;
  std::vector<type_annotation> invisible_type_annotations;
  std::vector<attribute> attributes;
  class_file() = default;
  class_file(uint32_t version, uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name);
  class_file(uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name);
  class_file(const std::string_view& name, const std::optional<std::string>& super_name);
  explicit class_file(const std::string_view& name);
  class_file(uint32_t version, uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields);
  class_file(uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields);
  class_file(const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields);
  class_file(const std::string_view& name, std::vector<field>&& fields);
  class_file(uint32_t version, uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields, std::vector<method>&& methods);
  class_file(uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields, std::vector<method>&& methods);
  class_file(const std::string_view& name, const std::optional<std::string>& super_name, std::vector<field>&& fields, std::vector<method>&& methods);
  class_file(const std::string_view& name, std::vector<field>&& fields, std::vector<method>&& methods);
  class_file(uint32_t version, uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<method>&& methods);
  class_file(uint16_t access_flags, const std::string_view& name, const std::optional<std::string>& super_name, std::vector<method>&& methods);
  class_file(const std::string_view& name, const std::optional<std::string>& super_name, std::vector<method>&& methods);
  class_file(const std::string_view& name, std::vector<method>&& methods);
  ~class_file() = default;
  class_file(const class_file& other) = default;
  class_file(class_file&& other) noexcept = default;
  class_file& operator=(const class_file& other) = default;
  class_file& operator=(class_file&& other) noexcept = default;
};
} // namespace cafe
