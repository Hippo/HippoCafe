#include "cafe/class_file.hpp"

#include <algorithm>
#include <sstream>

#include "cafe/class_writer.hpp"
#include "cafe/constants.hpp"
#include "visitor.hpp"

namespace cafe {
attribute::attribute(const std::string_view& name, const std::vector<int8_t>& data) : name(name), data(data) {
}
record_component::record_component(const std::string_view& name, const std::string_view& desc) :
    name(name), desc(desc) {
}
module_require::module_require(const std::string_view& module, uint16_t access_flags,
                               const std::optional<std::string>& version) :
    module(module), access_flags(access_flags), version(version) {
}
module_export::module_export(const std::string_view& package, uint16_t access_flags,
                             const std::vector<std::string>& modules) :
    package(package), access_flags(access_flags), modules(modules) {
}
module_open::module_open(const std::string_view& package, uint16_t access_flags,
                         const std::vector<std::string>& modules) :
    package(package), access_flags(access_flags), modules(modules) {
}
module_provide::module_provide(const std::string_view& service, const std::vector<std::string>& providers) :
    service(service), providers(providers) {
}
module::module(const std::string_view& name, uint16_t access_flags,
               const std::optional<std::string>& version) :name(name),
    access_flags(access_flags), version(version) {
}
code::code(const std::allocator<instruction>& allocator) : list(allocator) {
}
code::code(size_type count) : list(count) {
}
code::code(size_type count, const std::allocator<instruction>& allocator) : list(count, allocator) {
}
code::code(size_type count, const instruction& value) : list(count, value) {
}
code::code(const list& insns) : list(insns) {
}
code::code(list&& insns) : list(insns) {
}
code::code(const std::initializer_list<instruction>& insns) : list(insns) {
}
code::code(const std::initializer_list<instruction>& insns, const std::allocator<instruction>& allocator) :
    list(insns, allocator) {
}
code::code(uint16_t max_stack, uint16_t max_locals) : max_stack(max_stack), max_locals(max_locals) {
}
void code::add_label(const label& label) {
  emplace_back(label);
}
void code::add_insn(uint8_t opcode) {
  emplace_back(insn(opcode));
}
void code::add_var_insn(uint8_t opcode, uint16_t index) {
  emplace_back(var_insn(opcode, index));
}
void code::add_type_insn(uint8_t opcode, const std::string_view& type) {
  emplace_back(type_insn(opcode, type));
}
void code::add_field_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                          const std::string_view& descriptor) {
  emplace_back(field_insn(opcode, owner, name, descriptor));
}
void code::add_method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                           const std::string_view& descriptor) {
  emplace_back(method_insn(opcode, owner, name, descriptor));
}
void code::add_method_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                           const std::string_view& descriptor, bool interface) {
  emplace_back(method_insn(opcode, owner, name, descriptor, interface));
}
void code::add_iinc_insn(uint16_t index, int16_t value) {
  emplace_back(iinc_insn(index, value));
}
void code::add_push_insn(value value) {
  emplace_back(push_insn(value));
}
void code::add_branch_insn(uint8_t opcode, label target) {
  emplace_back(branch_insn(opcode, target));
}
void code::add_lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets) {
  emplace_back(lookup_switch_insn(default_target, targets));
}
void code::add_table_switch_insn(label default_target, int32_t low, int32_t high, const std::vector<label>& targets) {
  emplace_back(table_switch_insn(default_target, low, high, targets));
}
void code::add_multi_array_insn(const std::string_view& descriptor, uint8_t dims) {
  emplace_back(multi_array_insn(descriptor, dims));
}
void code::add_array_insn(const std::variant<uint8_t, std::string>& type) {
  emplace_back(array_insn(type));
}
void code::add_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                   method_handle handle, const std::vector<value>& args) {
  emplace_back(invoke_dynamic_insn(name, descriptor, handle, args));
}
code::const_iterator code::find_label(const label& lbl) const {
  return std::find_if(begin(), end(), [&](const auto& elem) {
    if (const auto l = std::get_if<label>(&elem)) {
      return *l == lbl;
    }
    return false;
  });
}
code::iterator code::find_label(const label& lbl) {
  return std::find_if(begin(), end(), [&](const auto& elem) {
    if (const auto l = std::get_if<label>(&elem)) {
      return *l == lbl;
    }
    return false;
  });
}
std::string code::to_string() const {
  return to_string(", ");
}
std::string code::to_string(const std::string_view& delim) const {
  std::ostringstream oss;
  oss << "{";
  bool first = true;
  for (const auto& insn : *this) {
    if (!first) {
      oss << delim;
    }
    first = false;
    oss << cafe::to_string(insn);
  }
  oss << "}";
  return oss.str();
}
field::field(uint16_t access_flags, const std::string_view& name, const std::string_view& desc) :
    access_flags(access_flags), name(name), desc(desc) {
}
method::method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc) :
    access_flags(access_flags), name(name), desc(desc) {
}
method::method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc, cafe::code&& body) :
    access_flags(access_flags), name(name), desc(desc), body(std::move(body)) {
}
method::method(uint16_t access_flags, const std::string_view& name, const std::string_view& desc,
               const cafe::code& body) : access_flags(access_flags), name(name), desc(desc), body(body) {
}
std::string method::name_desc(const std::string_view& separator) const {
  std::ostringstream oss;
  oss << name << separator << desc;
  return oss.str();
}
std::string method::name_desc() const {
  return name_desc("");
}
inner_class::inner_class(const std::string_view& name, const std::optional<std::string>& outer_name,
                         const std::optional<std::string>& inner_name, uint16_t access_flags) :
    name(name), outer_name(outer_name), inner_name(inner_name), access_flags(access_flags) {
}
class_file::class_file(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name) :
    version(version), access_flags(access_flags), name(name), super_name(super_name) {
}
class_file::class_file(uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name) :
    version(class_version::v8), access_flags(access_flags), name(name), super_name(super_name) {
}
class_file::class_file(const std::string_view& name, const std::optional<std::string>& super_name) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name(super_name) {
}
class_file::class_file(const std::string_view& name) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name("java/lang/Object") {
}
class_file::class_file(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<field>&& fields) :
    version(version), access_flags(access_flags), name(name), super_name(super_name), fields(std::move(fields)) {
}
class_file::class_file(uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<field>&& fields) :
    version(class_version::v8), access_flags(access_flags), name(name), super_name(super_name),
    fields(std::move(fields)) {
}
class_file::class_file(const std::string_view& name, const std::optional<std::string>& super_name,
                       std::vector<field>&& fields) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name(super_name),
    fields(std::move(fields)) {
}
class_file::class_file(const std::string_view& name, std::vector<field>&& fields) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name("java/lang/Object"),
    fields(std::move(fields)) {
}
class_file::class_file(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<field>&& fields,
                       std::vector<method>&& methods) :
    version(version), access_flags(access_flags), name(name), super_name(super_name), fields(std::move(fields)),
    methods(std::move(methods)) {
}
class_file::class_file(uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<field>&& fields,
                       std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flags), name(name), super_name(super_name),
    fields(std::move(fields)), methods(std::move(methods)) {
}
class_file::class_file(const std::string_view& name, const std::optional<std::string>& super_name,
                       std::vector<field>&& fields, std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name(super_name),
    fields(std::move(fields)), methods(std::move(methods)) {
}
class_file::class_file(const std::string_view& name, std::vector<field>&& fields, std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name("java/lang/Object"),
    fields(std::move(fields)), methods(std::move(methods)) {
}
class_file::class_file(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<method>&& methods) :
    version(version), access_flags(access_flags), name(name), super_name(super_name), methods(std::move(methods)) {
}
class_file::class_file(uint16_t access_flags, const std::string_view& name,
                       const std::optional<std::string>& super_name, std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flags), name(name), super_name(super_name),
    methods(std::move(methods)) {
}
class_file::class_file(const std::string_view& name, const std::optional<std::string>& super_name,
                       std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name(super_name),
    methods(std::move(methods)) {
}
class_file::class_file(const std::string_view& name, std::vector<method>&& methods) :
    version(class_version::v8), access_flags(access_flag::acc_public), name(name), super_name("java/lang/Object"),
    methods(std::move(methods)) {
}
} // namespace cafe
