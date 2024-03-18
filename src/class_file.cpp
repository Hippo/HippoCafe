#include "cafe/class_file.hpp"

#include "cafe/class_writer.hpp"
#include "visitor.hpp"

namespace cafe {
attribute::attribute(const std::string_view& name, const std::vector<int8_t>& data) : name(name), data(data) {
}
record_component::record_component(const std::string_view& name, const std::string_view& descriptor) :
    name(name), descriptor(descriptor) {
}
void record_component::visit(const std::string_view& name, const std::string_view& descriptor) {
  this->name = name;
  this->descriptor = descriptor;
}
void record_component::visit_signature(const std::string_view& signature) {
  this->signature = signature;
}
void record_component::visit_visible_annotation(const annotation& annotation) {
  visible_annotations.emplace_back(annotation);
}
void record_component::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations.emplace_back(annotation);
}
void record_component::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations.emplace_back(annotation);
}
void record_component::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations.emplace_back(annotation);
}
module_require::module_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version) :
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
module::module(const std::string_view& name, uint16_t access_flags, const std::string_view& version) :name(name),
    access_flags(access_flags), version(version) {
}
void module::visit(const std::string_view& name, uint16_t access_flags, const std::string_view& version) {
  this->name = name;
  this->access_flags = access_flags;
  this->version = version;
}
void module::visit_require(const std::string_view& module, uint16_t access_flags, const std::string_view& version) {
  requires.emplace_back(module, access_flags, version);
}
void module::visit_export(const std::string_view& package, uint16_t access_flags,
                          const std::vector<std::string>& modules) {
  exports.emplace_back(package, access_flags, modules);
}
void module::visit_open(const std::string_view& package, uint16_t access_flags,
                        const std::vector<std::string>& modules) {
  opens.emplace_back(package, access_flags, modules);
}
void module::visit_use(const std::string_view& service) {
  uses.emplace_back(service);
}
void module::visit_provide(const std::string_view& service, const std::vector<std::string>& providers) {
  provides.emplace_back(service, providers);
}
void code::visit(uint16_t max_stack, uint16_t max_locals) {
  this->max_stack = max_stack;
  this->max_locals = max_locals;
}
void code::visit_line_number(uint16_t line, label start) {
  line_numbers.emplace_back(line, start);
}
void code::visit_tcb(const tcb& try_catch) {
  tcbs.emplace_back(try_catch);
}
void code::visit_local(const local_var& local) {
  locals.emplace_back(local);
}
void code::visit_frame(const label& target, const frame& frame) {
  frames.emplace_back(target, frame);
}
void code::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations.emplace_back(annotation);
}
void code::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations.emplace_back(annotation);
}
void code::visit_label(const label& label) {
  emplace_back(label);
}
void code::visit_insn(uint8_t opcode) {
  emplace_back(insn(opcode));
}
void code::visit_var_insn(uint8_t opcode, uint16_t index) {
  emplace_back(var_insn(opcode, index));
}
void code::visit_type_insn(uint8_t opcode, const std::string_view& type) {
  emplace_back(type_insn(opcode, type));
}
void code::visit_ref_insn(uint8_t opcode, const std::string_view& owner, const std::string_view& name,
                          const std::string_view& descriptor) {
  emplace_back(ref_insn(opcode, owner, name, descriptor));
}
void code::visit_iinc_insn(uint16_t index, int16_t value) {
  emplace_back(iinc_insn(index, value));
}
void code::visit_push_insn(value value) {
  emplace_back(push_insn(value));
}
void code::visit_branch_insn(uint8_t opcode, label target) {
  emplace_back(branch_insn(opcode, target));
}
void code::visit_lookup_switch_insn(label default_target, const std::vector<std::pair<int32_t, label>>& targets) {
  emplace_back(lookup_switch_insn(default_target, targets));
}
void code::visit_table_switch_insn(label default_target, int32_t low, int32_t high, const std::vector<label>& targets) {
  emplace_back(table_switch_insn(default_target, low, high, targets));
}
void code::visit_multi_array_insn(const std::string_view& descriptor, uint8_t dims) {
  emplace_back(multi_array_insn(descriptor, dims));
}
void code::visit_array_insn(const std::variant<uint8_t, std::string>& type) {
  emplace_back(array_insn(type));
}
void code::visit_invoke_dynamic_insn(const std::string_view& name, const std::string_view& descriptor,
                                     method_handle handle, const std::vector<value>& args) {
  emplace_back(invoke_dynamic_insn(name, descriptor, handle, args));
}
field::field(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
}
void field::visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) {
  this->access_flags = access_flags;
  this->name = name;
  this->descriptor = descriptor;
}
void field::visit_constant_value(const value& constant_value) {
  this->constant_value = constant_value;
}
void field::visit_synthetic(bool synthetic) {
  this->synthetic = synthetic;
}
void field::visit_deprecated(bool deprecated) {
  this->deprecated = deprecated;
}
void field::visit_signature(const std::string_view& signature) {
  this->signature = signature;
}
void field::visit_visible_annotation(const annotation& annotation) {
  visible_annotations.emplace_back(annotation);
}
void field::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations.emplace_back(annotation);
}
void field::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations.emplace_back(annotation);
}
void field::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations.emplace_back(annotation);
}
method::method(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
}
void method::visit(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) {
  this->access_flags = access_flags;
  this->name = name;
  this->descriptor = descriptor;
}
code_visitor& method::visit_code(uint16_t max_stack, uint16_t max_locals) {
  code.max_stack = max_stack;
  code.max_locals = max_locals;
  return code;
}
void method::visit_exception(const std::string_view& exception) {
  exceptions.emplace_back(exception);
}
void method::visit_visible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
  if (visible_parameter_annotations.size() <= parameter) {
    visible_parameter_annotations.resize(parameter + 1);
  }
  visible_parameter_annotations[parameter].emplace_back(annotation);
}
void method::visit_invisible_parameter_annotation(uint8_t parameter, const annotation& annotation) {
  if (invisible_parameter_annotations.size() <= parameter) {
    invisible_parameter_annotations.resize(parameter + 1);
  }
  invisible_parameter_annotations[parameter].emplace_back(annotation);
}
void method::visit_annotation_default(const element_value& value) {
  annotation_default = value;
}
void method::visit_parameter(uint16_t access_flags, const std::string_view& name) {
  parameters.emplace_back(access_flags, name);
}
void method::visit_synthetic(bool synthetic) {
  this->synthetic = synthetic;
}
void method::visit_deprecated(bool deprecated) {
  this->deprecated = deprecated;
}
void method::visit_signature(const std::string_view& signature) {
  this->signature = signature;
}
void method::visit_visible_annotation(const annotation& annotation) {
  visible_annotations.emplace_back(annotation);
}
void method::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations.emplace_back(annotation);
}
void method::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations.emplace_back(annotation);
}
void method::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations.emplace_back(annotation);
}
inner_class::inner_class(const std::string_view& name, const std::string_view& outer_name,
                         const std::string_view& inner_name, uint16_t access_flags) :
    name(name), outer_name(outer_name), inner_name(inner_name), access_flags(access_flags) {
}
class_file::class_file(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::string_view& super_name) :
    version(version), access_flags(access_flags), name(name), super_name(super_name) {
}
void class_file::visit(uint32_t version, uint16_t access_flags, const std::string_view& name,
                       const std::string_view& super_name) {
  this->version = version;
  this->access_flags = access_flags;
  this->name = name;
  this->super_name = super_name;
}
void class_file::visit_interface(const std::string_view& name) {
  interfaces.emplace_back(name);
}
field_visitor& class_file::visit_field(uint16_t access_flags, const std::string_view& name,
                                       const std::string_view& descriptor) {
  fields.emplace_back(access_flags, name, descriptor);
  return fields.back();
}
method_visitor& class_file::visit_method(uint16_t access_flags, const std::string_view& name,
                                         const std::string_view& descriptor) {
  methods.emplace_back(access_flags, name, descriptor);
  return methods.back();
}
void class_file::visit_source_file(const std::string_view& source_file) {
  this->source_file = source_file;
}
void class_file::visit_inner_class(const std::string_view& name, const std::string_view& outer_name,
                                   const std::string_view& inner_name, uint16_t access_flags) {
  inner_classes.emplace_back(name, outer_name, inner_name, access_flags);
}
void class_file::visit_enclosing_method(const std::string_view& owner, const std::string_view& name,
                                        const std::string_view& descriptor) {
  enclosing_method_owner = owner;
  enclosing_method_name = name;
  enclosing_method_descriptor = descriptor;
}
void class_file::visit_source_debug_extension(const std::string_view& debug_extension) {
  source_debug_extension = debug_extension;
}
module_visitor& class_file::visit_module(const std::string_view& name, uint16_t access_flags,
                                         const std::string_view& version) {
  module = cafe::module(name, access_flags, version);
  return *module;
}
void class_file::visit_module_package(const std::string_view& package) {
  module_packages.emplace_back(package);
}
void class_file::visit_module_main_class(const std::string_view& main_class) {
  module_main_class = main_class;
}
void class_file::visit_nest_host(const std::string_view& host) {
  nest_host = host;
}
void class_file::visit_nest_member(const std::string_view& members) {
  nest_members.emplace_back(members);
}
record_component_visitor& class_file::visit_record_component(const std::string_view& name,
                                                             const std::string_view& descriptor) {
  record_components.emplace_back(name, descriptor);
  return record_components.back();
}
void class_file::visit_permitted_subclass(const std::string_view& subclass) {
  permitted_subclasses.emplace_back(subclass);
}
void class_file::visit_synthetic(bool synthetic) {
  this->synthetic = synthetic;
}
void class_file::visit_deprecated(bool deprecated) {
  this->deprecated = deprecated;
}
void class_file::visit_signature(const std::string_view& signature) {
  this->signature = signature;
}
void class_file::visit_visible_annotation(const annotation& annotation) {
  visible_annotations.emplace_back(annotation);
}
void class_file::visit_invisible_annotation(const annotation& annotation) {
  invisible_annotations.emplace_back(annotation);
}
void class_file::visit_visible_type_annotation(const type_annotation& annotation) {
  visible_type_annotations.emplace_back(annotation);
}
void class_file::visit_invisible_type_annotation(const type_annotation& annotation) {
  invisible_type_annotations.emplace_back(annotation);
}
void record_component::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes.emplace_back(name, data);
}
void code::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes.emplace_back(name, data);
}
void field::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes.emplace_back(name, data);
}
void method::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes.emplace_back(name, data);
}
void class_file::visit_attribute(const std::string_view& name, const std::vector<int8_t>& data) {
  attributes.emplace_back(name, data);
}

void record_component::visit_end() {
}
void module::visit_end() {
}
void code::visit_end() {
}
void field::visit_end() {
}
void method::visit_end() {
}
void class_file::visit_end() {
}

void class_file::accept(class_visitor& visitor) const {
  visitor.visit(version, access_flags, name, super_name);
  for (const auto& inter : interfaces) {
    visitor.visit_interface(inter);
  }
  if (!source_file.empty()) {
    visitor.visit_source_file(source_file);
  }
  for (const auto& inner : inner_classes) {
    visitor.visit_inner_class(inner.name, inner.outer_name, inner.inner_name, inner.access_flags);
  }
  if (!enclosing_method_owner.empty() && !enclosing_method_name.empty() && !enclosing_method_descriptor.empty()) {
    visitor.visit_enclosing_method(enclosing_method_owner, enclosing_method_name, enclosing_method_descriptor);
  }
  if (!source_debug_extension.empty()) {
    visitor.visit_source_debug_extension(source_debug_extension);
  }
  if (module) {
    auto& mod = visitor.visit_module(module->name, module->access_flags, module->version);
    module->accept(mod);
  }
  for (const auto& package : module_packages) {
    visitor.visit_module_package(package);
  }
  if (!module_main_class.empty()) {
    visitor.visit_module_main_class(module_main_class);
  }
  if (!nest_host.empty()) {
    visitor.visit_nest_host(nest_host);
  }
  for (const auto& member : nest_members) {
    visitor.visit_nest_host(member);
  }
  for (const auto& comp : record_components) {
    auto& record = visitor.visit_record_component(comp.name, comp.descriptor);
    comp.accept(record);
  }
  for (const auto& cls : permitted_subclasses) {
    visitor.visit_permitted_subclass(cls);
  }
  if (synthetic) {
    visitor.visit_synthetic(true);
  }
  if (deprecated) {
    visitor.visit_deprecated(true);
  }
  if (!signature.empty()) {
    visitor.visit_signature(signature);
  }
  for (const auto& anno : visible_annotations) {
    visitor.visit_visible_annotation(anno);
  }
  for (const auto& anno : invisible_annotations) {
    visitor.visit_invisible_annotation(anno);
  }
  for (const auto& anno : visible_type_annotations) {
    visitor.visit_visible_type_annotation(anno);
  }
  for (const auto& anno : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(anno);
  }
  for (const auto& attr : attributes) {
    visitor.visit_attribute(attr.name, attr.data);
  }
  for (const auto& field : fields) {
    auto& f = visitor.visit_field(field.access_flags, field.name, field.descriptor);
    field.accept(f);
  }
  for (const auto& method : methods) {
    auto& m = visitor.visit_method(method.access_flags, method.name, method.descriptor);
    method.accept(m);
  }
  visitor.visit_end();
}
void record_component::accept(record_component_visitor& visitor) const {
  if (!signature.empty()) {
    visitor.visit_signature(signature);
  }
  for (const auto& anno : visible_annotations) {
    visitor.visit_visible_annotation(anno);
  }
  for (const auto& anno : invisible_annotations) {
    visitor.visit_invisible_annotation(anno);
  }
  for (const auto& anno : visible_type_annotations) {
    visitor.visit_visible_type_annotation(anno);
  }
  for (const auto& anno : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(anno);
  }
  for (const auto& attr : attributes) {
    visitor.visit_attribute(attr.name, attr.data);
  }
  visitor.visit_end();
}
void module::accept(module_visitor& visitor) const {
  for (const auto& req : requires) {
    visitor.visit_require(req.module, req.access_flags, req.version);
  }
  for (const auto& exp : exports) {
    visitor.visit_export(exp.package, exp.access_flags, exp.modules);
  }
  for (const auto& op : opens) {
    visitor.visit_open(op.package, op.access_flags, op.modules);
  }
  for (const auto& use : uses) {
    visitor.visit_use(use);
  }
  for (const auto& prov : provides) {
    visitor.visit_provide(prov.service, prov.providers);
  }
  visitor.visit_end();
}
void code::accept(code_visitor& visitor) const {
  for (const auto& in : *this) {
    std::visit(instruction_visitor(visitor), in);
  }
  for (const auto& [start, label] : line_numbers) {
    visitor.visit_line_number(start, label);
  }
  for (const auto& tcb : tcbs) {
    visitor.visit_tcb(tcb);
  }
  for (const auto& local : locals) {
    visitor.visit_local(local);
  }
  for (const auto& [pos, frame] : frames) {
    visitor.visit_frame(pos, frame);
  }
  for (const auto& anno : visible_type_annotations) {
    visitor.visit_visible_type_annotation(anno);
  }
  for (const auto& anno : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(anno);
  }
  for (const auto& attr : attributes) {
    visitor.visit_attribute(attr.name, attr.data);
  }
  visitor.visit_end();
}
void field::accept(field_visitor& visitor) const {
  if (constant_value) {
    visitor.visit_constant_value(*constant_value);
  }
  if (synthetic) {
    visitor.visit_synthetic(true);
  }
  if (deprecated) {
    visitor.visit_deprecated(true);
  }
  if (!signature.empty()) {
    visitor.visit_signature(signature);
  }
  for (const auto& anno : visible_annotations) {
    visitor.visit_visible_annotation(anno);
  }
  for (const auto& anno : invisible_annotations) {
    visitor.visit_invisible_annotation(anno);
  }
  for (const auto& anno : visible_type_annotations) {
    visitor.visit_visible_type_annotation(anno);
  }
  for (const auto& anno : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(anno);
  }
  for (const auto& attr : attributes) {
    visitor.visit_attribute(attr.name, attr.data);
  }
  visitor.visit_end();
}
void method::accept(method_visitor& visitor) const {
  for (const auto& ex : exceptions) {
    visitor.visit_exception(ex);
  }
  for (auto i = 0; i < visible_parameter_annotations.size(); i++) {
    for (const auto& anno : visible_parameter_annotations[i]) {
      visitor.visit_visible_parameter_annotation(static_cast<uint8_t>(i), anno);
    }
  }
  for (auto i = 0; i < invisible_parameter_annotations.size(); i++) {
    for (const auto& anno : invisible_parameter_annotations[i]) {
      visitor.visit_invisible_parameter_annotation(static_cast<uint8_t>(i), anno);
    }
  }
  if (annotation_default) {
    visitor.visit_annotation_default(*annotation_default);
  }
  for (const auto& [p_acc, p_name] : parameters) {
    visitor.visit_parameter(p_acc, p_name);
  }
  if (synthetic) {
    visitor.visit_synthetic(true);
  }
  if (deprecated) {
    visitor.visit_deprecated(true);
  }
  if (!signature.empty()) {
    visitor.visit_signature(signature);
  }
  for (const auto& anno : visible_annotations) {
    visitor.visit_visible_annotation(anno);
  }
  for (const auto& anno : invisible_annotations) {
    visitor.visit_invisible_annotation(anno);
  }
  for (const auto& anno : visible_type_annotations) {
    visitor.visit_visible_type_annotation(anno);
  }
  for (const auto& anno : invisible_type_annotations) {
    visitor.visit_invisible_type_annotation(anno);
  }
  for (const auto& attr : attributes) {
    visitor.visit_attribute(attr.name, attr.data);
  }
  if (!code.empty()) {
    auto& c = visitor.visit_code(code.max_stack, code.max_locals);
    code.accept(c);
  }
  visitor.visit_end();
}
std::istream& operator>>(std::istream& is, class_file& cf) {
  class_reader reader(is);
  reader.accept(cf);
  return is;
}
std::ostream& operator<<(std::ostream& os, const class_file& cf) {
  class_writer writer;
  cf.accept(writer);
  const auto data = writer.write();
  os.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return os;
}
} // namespace cafe
