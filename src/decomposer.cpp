#include "decomposer.hpp"

#include "cafe/analysis.hpp"
#include "cafe/constants.hpp"

#include <iostream>

namespace cafe {
decomposer_ctx::decomposer_ctx(data_writer& writer) : writer(writer) {
}
uint16_t decomposer_ctx::get_vaule(const value& value) {
  return pool.get_value(bsm_buffer, value);
}
uint16_t decomposer_ctx::get_bsm_index(const method_handle& handle, const std::vector<value>& args) {
  return bsm_buffer.get_bsm_index(pool, handle, args);
}
class_decomposer::class_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
class_file class_decomposer::decompose(const class_model& class_model) {
  class_file cf;
  cf.magic = 0xCAFEBABE;
  cf.minor_version = static_cast<uint16_t>(class_model.version & 0xFFFF);
  cf.major_version = static_cast<uint16_t>(class_model.version >> 16);
  cf.access_flags = class_model.access_flags;
  cf.this_class = ctx_.pool.get_class(class_model.name);
  cf.super_class = class_model.super_name.empty() ? 0 : ctx_.pool.get_class(class_model.super_name);
  cf.interfaces.reserve(class_model.interfaces.size());
  for (const auto& interface : class_model.interfaces) {
    cf.interfaces.emplace_back(ctx_.pool.get_class(interface));
  }
  cf.fields.reserve(class_model.fields.size());
  field_decomposer field_decomposer(ctx_);
  for (const auto& field : class_model.fields) {
    cf.fields.emplace_back(field_decomposer.decompose(field));
  }
  cf.methods.reserve(class_model.methods.size());
  method_decomposer method_decomposer(ctx_);
  for (const auto& method : class_model.methods) {
    cf.methods.emplace_back(method_decomposer.decompose(method));
  }

  if (class_model.synthetic) {
    ctx_.pool.get_utf("Synthetic");
    cf.attributes.emplace_back(attribute::synthetic{});
  }
  if (class_model.deprecated) {
    ctx_.pool.get_utf("Deprecated");
    cf.attributes.emplace_back(attribute::deprecated{});
  }
  if (!class_model.signature.empty()) {
    ctx_.pool.get_utf("Signature");
    cf.attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(class_model.signature)});
  }
  if (!class_model.source_file.empty()) {
    ctx_.pool.get_utf("SourceFile");
    cf.attributes.emplace_back(attribute::source_file{ctx_.pool.get_utf(class_model.source_file)});
  }
  if (!class_model.inner_classes.empty()) {
    ctx_.pool.get_utf("InnerClasses");
    std::vector<attribute::inner_classes::inner_class> inner_classes;
    inner_classes.reserve(class_model.inner_classes.size());
    for (const auto& inner : class_model.inner_classes) {
      inner_classes.emplace_back(
          attribute::inner_classes::inner_class{ctx_.pool.get_class(inner.name), ctx_.pool.get_class(inner.outer_name),
                                                ctx_.pool.get_utf(inner.inner_name), inner.access_flags});
    }
    cf.attributes.emplace_back(attribute::inner_classes{inner_classes});
  }

  if (!class_model.enclosing_owner.empty() && !class_model.enclosing_name.empty() &&
      !class_model.enclosing_descriptor.empty()) {
    ctx_.pool.get_utf("EnclosingMethod");
    auto owner_index = ctx_.pool.get_class(class_model.enclosing_owner);
    auto name_and_type_index =
        ctx_.pool.get_name_and_type(class_model.enclosing_name, class_model.enclosing_descriptor);
    cf.attributes.emplace_back(attribute::enclosing_method{owner_index, name_and_type_index});
  }

  if (!class_model.source_debug_extension.empty()) {
    ctx_.pool.get_utf("SourceDebugExtension");
    std::vector<uint8_t> vec(class_model.source_debug_extension.begin(), class_model.source_debug_extension.end());
    cf.attributes.emplace_back(attribute::source_debug_extension{vec});
  }

  if (class_model.module) {
    ctx_.pool.get_utf("Module");
    auto name_index = ctx_.pool.get_utf(class_model.module->name);
    auto version_index = ctx_.pool.get_utf(class_model.module->version);
    std::vector<attribute::module::require>
      requires;
    requires.reserve(class_model.module->requires.size());
    for (const auto& req : class_model.module->requires) {
      auto index = ctx_.pool.get_module(req.name);
      auto ver_index = static_cast<uint16_t>(req.version.empty() ? 0 : ctx_.pool.get_utf(req.version));
      requires.emplace_back(attribute::module::require{index, req.access_flags, ver_index});
    }
    std::vector<attribute::module::mod_export> exports;
    exports.reserve(class_model.module->exports.size());
    for (const auto& exp : class_model.module->exports) {
      auto index = ctx_.pool.get_package(exp.package);
      std::vector<uint16_t> to_index;
      to_index.reserve(exp.modules.size());
      for (const auto& mod : exp.modules) {
        to_index.emplace_back(ctx_.pool.get_module(mod));
      }
      exports.emplace_back(attribute::module::mod_export{index, exp.access_flags, to_index});
    }
    std::vector<attribute::module::open> opens;
    opens.reserve(class_model.module->opens.size());
    for (const auto& open : class_model.module->opens) {
      auto index = ctx_.pool.get_package(open.package);
      std::vector<uint16_t> to_index;
      to_index.reserve(open.modules.size());
      for (const auto& mod : open.modules) {
        to_index.emplace_back(ctx_.pool.get_module(mod));
      }
      opens.emplace_back(attribute::module::open{index, open.access_flags, to_index});
    }
    std::vector<uint16_t> uses;
    uses.reserve(class_model.module->uses.size());
    for (const auto& use : class_model.module->uses) {
      uses.emplace_back(ctx_.pool.get_class(use));
    }
    std::vector<attribute::module::provide> provides;
    provides.reserve(class_model.module->provides.size());
    for (const auto& prov : class_model.module->provides) {
      auto index = ctx_.pool.get_class(prov.service);
      std::vector<uint16_t> with_index;
      with_index.reserve(prov.providers.size());
      for (const auto& provider : prov.providers) {
        with_index.emplace_back(ctx_.pool.get_class(provider));
      }
      provides.emplace_back(attribute::module::provide{index, with_index});
    }
    cf.attributes.emplace_back(attribute::module{name_index, class_model.module->access_flags, version_index, requires,
                                                 exports, opens, uses, provides});
  }

  if (!class_model.module_packages.empty()) {
    ctx_.pool.get_utf("ModulePackages");
    std::vector<uint16_t> package_indices;
    package_indices.reserve(class_model.module_packages.size());
    for (const auto& package : class_model.module_packages) {
      package_indices.emplace_back(ctx_.pool.get_package(package));
    }
    cf.attributes.emplace_back(attribute::module_packages{package_indices});
  }

  if (!class_model.module_main_class.empty()) {
    ctx_.pool.get_utf("ModuleMainClass");
    auto index = ctx_.pool.get_class(class_model.module_main_class);
    cf.attributes.emplace_back(attribute::module_main_class{index});
  }

  if (!class_model.nest_host.empty()) {
    ctx_.pool.get_utf("NestHost");
    auto index = ctx_.pool.get_class(class_model.nest_host);
    cf.attributes.emplace_back(attribute::nest_host{index});
  }

  if (!class_model.nest_members.empty()) {
    ctx_.pool.get_utf("NestMembers");
    std::vector<uint16_t> indices;
    indices.reserve(class_model.nest_members.size());
    for (const auto& member : class_model.nest_members) {
      indices.emplace_back(ctx_.pool.get_class(member));
    }
    cf.attributes.emplace_back(attribute::nest_members{indices});
  }

  if (!class_model.permitted_subclasses.empty()) {
    ctx_.pool.get_utf("PermittedSubclasses");
    std::vector<uint16_t> indices;
    indices.reserve(class_model.permitted_subclasses.size());
    for (const auto& subclass : class_model.permitted_subclasses) {
      indices.emplace_back(ctx_.pool.get_class(subclass));
    }
    cf.attributes.emplace_back(attribute::permitted_subclasses{indices});
  }

  if (!class_model.record_components.empty()) {
    ctx_.pool.get_utf("Record");
    std::vector<attribute::record::component> components;
    components.reserve(class_model.record_components.size());
    record_decomposer record_decomposer(ctx_);
    for (const auto& component : class_model.record_components) {
      components.emplace_back(record_decomposer.decompose(component));
    }
    cf.attributes.emplace_back(attribute::record{components});
  }

  if (!class_model.visible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(class_model.visible_annotations.size());
    for (const auto& anno : class_model.visible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    cf.attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!class_model.invisible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(class_model.invisible_annotations.size());
    for (const auto& anno : class_model.invisible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    cf.attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!class_model.visible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(class_model.visible_type_annotations.size());
    for (const auto& anno : class_model.visible_type_annotations) {
      annotations.emplace_back(decompose(class_model, anno));
    }
    cf.attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!class_model.invisible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(class_model.invisible_type_annotations.size());
    for (const auto& anno : class_model.invisible_type_annotations) {
      annotations.emplace_back(decompose(class_model, anno));
    }
    cf.attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }

  cf.constant_pool = ctx_.pool;
  if (!ctx_.bsm_buffer.bsms.empty()) {
    ctx_.pool.get_utf("BootstrapMethods");
    cf.attributes.emplace_back(attribute::bootstrap_methods{ctx_.bsm_buffer.bsms});
  }

  return cf;
}
attribute::type_annotation class_decomposer::decompose(const class_model& class_model,
                                                       const type_annotation& annotation) {
  auto target_info = std::visit(
      [this, &class_model](auto&& arg) -> attribute::type_annotation_target {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, target::type_parameter>) {
          return attribute::type_parameter{arg.index};
        } else if constexpr (std::is_same_v<T, target::supertype>) {
          uint16_t index{};
          if (arg.name == class_model.super_name) {
            index = 65535;
          } else {
            for (auto i = 0; i < class_model.interfaces.size(); i++) {
              if (arg.name == class_model.interfaces[i]) {
                index = static_cast<uint16_t>(i);
                break;
              }
            }
          }
          return attribute::supertype{index};
        } else if constexpr (std::is_same_v<T, target::type_parameter_bound>) {
          return attribute::type_parameter_bound{arg.type_parameter_index, arg.bound_index};
        } else {
          throw std::runtime_error("Unknown target type");
        }
      },
      annotation.target_info);
  annotation_decomposer annotation_decomposer(ctx_);
  attribute::type_path path;
  path.paths.reserve(annotation.target_path.path.size());
  for (const auto& [kind, index] : annotation.target_path.path) {
    path.paths.emplace_back(attribute::type_path::path{kind, index});
  }
  std::vector<attribute::element_pair> pairs;
  pairs.reserve(annotation.values.size());
  for (const auto& [name, val] : annotation.values) {
    auto name_index = ctx_.pool.get_utf(name);
    auto value = annotation_decomposer.decompose(val);
    pairs.emplace_back(attribute::element_pair{name_index, value});
  }
  auto desc_index = ctx_.pool.get_utf(annotation.descriptor);
  return attribute::type_annotation{annotation.target_type, target_info, path, desc_index, pairs};
}
field_decomposer::field_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
field_info field_decomposer::decompose(const field_model& field_model) {
  field_info field;
  field.access_flags = field_model.access_flags;
  field.name_index = ctx_.pool.get_utf(field_model.name);
  field.descriptor_index = ctx_.pool.get_utf(field_model.descriptor);
  if (field_model.synthetic) {
    ctx_.pool.get_utf("Synthetic");
    field.attributes.emplace_back(attribute::synthetic{});
  }
  if (field_model.deprecated) {
    ctx_.pool.get_utf("Deprecated");
    field.attributes.emplace_back(attribute::deprecated{});
  }
  if (field_model.constant_value) {
    ctx_.pool.get_utf("ConstantValue");
    field.attributes.emplace_back(
        attribute::constant_value{ctx_.pool.get_value(ctx_.bsm_buffer, *field_model.constant_value)});
  }
  if (!field_model.signature.empty()) {
    ctx_.pool.get_utf("Signature");
    field.attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(field_model.signature)});
  }
  if (!field_model.visible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(field_model.visible_annotations.size());
    for (const auto& anno : field_model.visible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    field.attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!field_model.invisible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(field_model.invisible_annotations.size());
    for (const auto& anno : field_model.invisible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    field.attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!field_model.visible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(field_model.visible_type_annotations.size());
    for (const auto& anno : field_model.visible_type_annotations) {
      annotation_decomposer annotation_decomposer(ctx_);
      attribute::type_path path;
      path.paths.reserve(anno.target_path.path.size());
      for (const auto& [kind, index] : anno.target_path.path) {
        path.paths.emplace_back(attribute::type_path::path{kind, index});
      }
      std::vector<attribute::element_pair> pairs;
      pairs.reserve(anno.values.size());
      for (const auto& [name, val] : anno.values) {
        auto name_index = ctx_.pool.get_utf(name);
        auto value = annotation_decomposer.decompose(val);
        pairs.emplace_back(attribute::element_pair{name_index, value});
      }
      auto desc_index = ctx_.pool.get_utf(anno.descriptor);
      annotations.emplace_back(
          attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    field.attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!field_model.invisible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(field_model.invisible_type_annotations.size());
    for (const auto& anno : field_model.invisible_type_annotations) {
      annotation_decomposer annotation_decomposer(ctx_);
      attribute::type_path path;
      path.paths.reserve(anno.target_path.path.size());
      for (const auto& [kind, index] : anno.target_path.path) {
        path.paths.emplace_back(attribute::type_path::path{kind, index});
      }
      std::vector<attribute::element_pair> pairs;
      pairs.reserve(anno.values.size());
      for (const auto& [name, val] : anno.values) {
        auto name_index = ctx_.pool.get_utf(name);
        auto value = annotation_decomposer.decompose(val);
        pairs.emplace_back(attribute::element_pair{name_index, value});
      }
      auto desc_index = ctx_.pool.get_utf(anno.descriptor);
      annotations.emplace_back(
          attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    field.attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }
  return field;
}
method_decomposer::method_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
method_info method_decomposer::decompose(const method_model& method_model) {
  method_info method;
  method.access_flags = method_model.access_flags;
  method.name_index = ctx_.pool.get_utf(method_model.name);
  method.descriptor_index = ctx_.pool.get_utf(method_model.descriptor);
  if (method_model.synthetic) {
    ctx_.pool.get_utf("Synthetic");
    method.attributes.emplace_back(attribute::synthetic{});
  }
  if (method_model.deprecated) {
    ctx_.pool.get_utf("Deprecated");
    method.attributes.emplace_back(attribute::deprecated{});
  }
  if (!method_model.signature.empty()) {
    ctx_.pool.get_utf("Signature");
    method.attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(method_model.signature)});
  }
  if (!method_model.visible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(method_model.visible_annotations.size());
    for (const auto& anno : method_model.visible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    method.attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!method_model.invisible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(method_model.invisible_annotations.size());
    for (const auto& anno : method_model.invisible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    method.attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!method_model.visible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(method_model.visible_type_annotations.size());
    for (const auto& anno : method_model.visible_type_annotations) {
      annotations.emplace_back(decompose(method_model, anno));
    }
    method.attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!method_model.invisible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(method_model.invisible_type_annotations.size());
    for (const auto& anno : method_model.invisible_type_annotations) {
      annotations.emplace_back(decompose(method_model, anno));
    }
    method.attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }
  if (!method_model.visible_parameter_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleParameterAnnotations");
    std::vector<std::vector<attribute::annotation>> parameter_annotations;
    parameter_annotations.reserve(method_model.visible_parameter_annotations.size());
    annotation_decomposer annotation_decomposer(ctx_);
    for (const auto& annos : method_model.visible_parameter_annotations) {
      std::vector<attribute::annotation> annotations;
      annotations.reserve(annos.size());
      for (const auto& anno : annos) {
        annotations.emplace_back(annotation_decomposer.decompose(anno));
      }
      parameter_annotations.emplace_back(annotations);
    }
    method.attributes.emplace_back(attribute::runtime_visible_parameter_annotations{parameter_annotations});
  }
  if (!method_model.invisible_parameter_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleParameterAnnotations");
    std::vector<std::vector<attribute::annotation>> parameter_annotations;
    parameter_annotations.reserve(method_model.invisible_parameter_annotations.size());
    annotation_decomposer annotation_decomposer(ctx_);
    for (const auto& annos : method_model.invisible_parameter_annotations) {
      std::vector<attribute::annotation> annotations;
      annotations.reserve(annos.size());
      for (const auto& anno : annos) {
        annotations.emplace_back(annotation_decomposer.decompose(anno));
      }
      parameter_annotations.emplace_back(annotations);
    }
    method.attributes.emplace_back(attribute::runtime_invisible_parameter_annotations{parameter_annotations});
  }
  if (method_model.annotation_default) {
    ctx_.pool.get_utf("AnnotationDefault");
    annotation_decomposer annotation_decomposer(ctx_);
    method.attributes.emplace_back(
        attribute::annotation_default{annotation_decomposer.decompose(*method_model.annotation_default)});
  }
  if (!method_model.method_parameters.empty()) {
    ctx_.pool.get_utf("MethodParameters");
    std::vector<attribute::method_parameters::parameter> parameters;
    parameters.reserve(method_model.method_parameters.size());
    for (const auto& [access_flags, name] : method_model.method_parameters) {
      parameters.emplace_back(
          attribute::method_parameters::parameter{name.empty() ? uint16_t{0} : ctx_.pool.get_utf(name), access_flags});
    }
    method.attributes.emplace_back(attribute::method_parameters{parameters});
  }
  if (!method_model.exceptions.empty()) {
    ctx_.pool.get_utf("Exceptions");
    std::vector<uint16_t> exceptions;
    exceptions.reserve(method_model.exceptions.size());
    for (const auto& exception : method_model.exceptions) {
      exceptions.emplace_back(ctx_.pool.get_class(exception));
    }
    method.attributes.emplace_back(attribute::exceptions{exceptions});
  }
  if (!method_model.code.empty()) {
    ctx_.pool.get_utf("Code");
    code_decomposer code_decomposer(ctx_);
    method.attributes.emplace_back(attribute::code{code_decomposer.decompose(method_model.code)});
  }
  return method;
}
attribute::type_annotation method_decomposer::decompose(const method_model& method_model,
                                                        const type_annotation& annotation) {
  auto target_info = std::visit(
      [this, &method_model](auto&& arg) -> attribute::type_annotation_target {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, target::type_parameter>) {
          return attribute::type_parameter{arg.index};
        } else if constexpr (std::is_same_v<T, target::type_parameter_bound>) {
          return attribute::type_parameter_bound{arg.type_parameter_index, arg.bound_index};
        } else if constexpr (std::is_same_v<T, target::empty>) {
          return attribute::empty{};
        } else if constexpr (std::is_same_v<T, target::formal_parameter>) {
          return attribute::formal_parameter{arg.index};
        } else if constexpr (std::is_same_v<T, target::throws>) {
          uint16_t index{};
          for (auto i = 0; i < method_model.exceptions.size(); i++) {
            if (arg.name == method_model.exceptions[i]) {
              index = static_cast<uint16_t>(i);
              break;
            }
          }
          return attribute::throws{index};
        }
        throw std::runtime_error("Unknown target type");
      },
      annotation.target_info);
  annotation_decomposer annotation_decomposer(ctx_);
  attribute::type_path path;
  path.paths.reserve(annotation.target_path.path.size());
  for (const auto& [kind, index] : annotation.target_path.path) {
    path.paths.emplace_back(attribute::type_path::path{kind, index});
  }
  std::vector<attribute::element_pair> pairs;
  pairs.reserve(annotation.values.size());
  for (const auto& [name, val] : annotation.values) {
    auto name_index = ctx_.pool.get_utf(name);
    auto value = annotation_decomposer.decompose(val);
    pairs.emplace_back(attribute::element_pair{name_index, value});
  }
  auto desc_index = ctx_.pool.get_utf(annotation.descriptor);
  return attribute::type_annotation{annotation.target_type, target_info, path, desc_index, pairs};
}
code_decomposer::code_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
uint16_t code_decomposer::get_label(const label& label) const {
  for (const auto& [l, p] : labels_) {
    if (l == label) {
      return static_cast<uint16_t>(p);
    }
  }
  return 0;
}

struct unwritten_branch {
  label target;
  bool wide;
  size_t instruction_pos;
};

attribute::code code_decomposer::decompose(const code& code) {
  std::vector<std::pair<label, size_t>> estimated_pos;
  estimated_pos.reserve(code.size());
  size_t code_size = 0;
  for (const auto& instr : code) {
    auto size = std::visit(analysis::max_size(), instr);
    if (auto l = std::get_if<label>(&instr)) {
      estimated_pos.emplace_back(*l, code_size);
    }
    code_size += size;
  }
  auto pos = [&estimated_pos](const label& label) -> size_t {
    for (const auto& [l, p] : estimated_pos) {
      if (l == label) {
        return p;
      }
    }
    return 0;
  };
  std::vector<uint8_t> bytecode;
  std::vector<std::pair<size_t, unwritten_branch>> branches;
  vecstreambuf buf(bytecode);
  std::ostream os(&buf);
  data_writer writer(os);
  for (const auto& instr : code) {
    std::visit(
        [this, &pos, &writer, &bytecode, &branches](const auto& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, label>) {
            labels_.emplace_back(arg, bytecode.size());
          } else if constexpr (std::is_same_v<T, insn>) {
            writer.write_u8(arg.opcode);
          } else if constexpr (std::is_same_v<T, var_insn>) {
            if (arg.index > std::numeric_limits<uint8_t>::max()) {
              writer.write_u8(op::wide);
              writer.write_u8(arg.opcode);
              writer.write_u16(arg.index);
            } else if (arg.index <= 3) {
              switch (arg.opcode) {
                case op::iload:
                  writer.write_u8(static_cast<uint8_t>(op::iload_0 + arg.index));
                  break;
                case op::dload:
                  writer.write_u8(static_cast<uint8_t>(op::dload_0 + arg.index));
                  break;
                case op::fload:
                  writer.write_u8(static_cast<uint8_t>(op::fload_0 + arg.index));
                  break;
                case op::aload:
                  writer.write_u8(static_cast<uint8_t>(op::aload_0 + arg.index));
                  break;
                case op::lload:
                  writer.write_u8(static_cast<int8_t>(op::lload_0 + arg.index));
                  break;
                case op::istore:
                  writer.write_u8(static_cast<uint8_t>(op::istore_0 + arg.index));
                  break;
                case op::dstore:
                  writer.write_u8(static_cast<uint8_t>(op::dstore_0 + arg.index));
                  break;
                case op::fstore:
                  writer.write_u8(static_cast<uint8_t>(op::fstore_0 + arg.index));
                  break;
                case op::astore:
                  writer.write_u8(static_cast<uint8_t>(op::astore_0 + arg.index));
                  break;
                case op::lstore:
                  writer.write_u8(static_cast<uint8_t>(op::lstore_0 + arg.index));
                  break;
                default:
                  throw std::runtime_error("Unknown var_insn opcode");
              }
            } else {
              writer.write_u8(arg.opcode);
              writer.write_u8(static_cast<uint8_t>(arg.index));
            }
          } else if constexpr (std::is_same_v<T, type_insn>) {
            auto index = ctx_.pool.get_class(arg.type);
            writer.write_u8(arg.opcode);
            writer.write_u16(index);
          } else if constexpr (std::is_same_v<T, ref_insn>) {
            uint16_t index;
            switch (arg.opcode) {
              case op::getfield:
              case op::getstatic:
              case op::putfield:
              case op::putstatic:
                index = ctx_.pool.get_field_ref(arg.owner, arg.name, arg.descriptor);
                break;
              case op::invokevirtual:
              case op::invokestatic:
              case op::invokespecial:
                index = ctx_.pool.get_method_ref(arg.owner, arg.name, arg.descriptor);
                break;
              case op::invokeinterface:
                index = ctx_.pool.get_interface_method_ref(arg.owner, arg.name, arg.descriptor);
                break;
              default:
                throw std::runtime_error("Unknown ref_insn opcode");
            }
            writer.write_u8(arg.opcode);
            writer.write_u16(index);
            if (arg.opcode == op::invokeinterface) {
              auto parsed_args = parse_method_descriptor(arg.descriptor).first;
              auto size = 0;
              for (const auto& ar : parsed_args) {
                size += cafe::size(ar);
              }
              writer.write_u8(size);
              writer.write_u8(0);
            }
          } else if constexpr (std::is_same_v<T, iinc_insn>) {
            if (arg.index > std::numeric_limits<uint8_t>::max() || arg.value > std::numeric_limits<int8_t>::max() ||
                arg.value < std::numeric_limits<int8_t>::min()) {
              writer.write_u8(op::wide);
              writer.write_u8(op::iinc);
              writer.write_u16(arg.index);
              writer.write_i16(arg.value);
            } else {
              writer.write_u8(op::iinc);
              writer.write_u8(static_cast<uint8_t>(arg.index));
              writer.write_i8(static_cast<int8_t>(arg.value));
            }
          } else if constexpr (std::is_same_v<T, push_insn>) {
            auto opcode = arg.opcode();
            if (opcode == op::bipush) {
              writer.write_u8(op::bipush);
              writer.write_i8(static_cast<int8_t>(std::get<int32_t>(arg.val)));
            } else if (opcode == op::sipush) {
              writer.write_u8(op::sipush);
              writer.write_i16(static_cast<int16_t>(std::get<int32_t>(arg.val)));
            } else if (opcode != op::ldc) {
              writer.write_u8(opcode);
            } else {
              auto index = ctx_.get_vaule(arg.val);
              if (std::holds_alternative<int64_t>(arg.val) || std::holds_alternative<double>(arg.val)) {
                writer.write_u8(op::ldc2_w);
                writer.write_u16(index);
              } else if (index > std::numeric_limits<uint8_t>::max()) {
                writer.write_u8(op::ldc_w);
                writer.write_u16(index);
              } else {
                writer.write_u8(op::ldc);
                writer.write_u8(static_cast<uint8_t>(index));
              }
            }
          } else if constexpr (std::is_same_v<T, branch_insn>) {
            auto estimated_target = pos(arg.target);
            uint8_t opcode;
            bool wide = false;
            bool wide_jmp = estimated_target > std::numeric_limits<int16_t>::max();
            switch (arg.opcode) {
              case op::goto_:
              case op::goto_w:
                wide = wide_jmp;
                opcode = wide ? op::goto_w : op::goto_;
                break;
              case op::jsr:
              case op::jsr_w:
                wide = wide_jmp;
                opcode = wide ? op::jsr_w : op::jsr;
                break;
              default:
                opcode = arg.opcode;
                break;
            }
            auto insn_pos = bytecode.size();
            writer.write_u8(opcode);
            branches.emplace_back(bytecode.size(), unwritten_branch{arg.target, wide, insn_pos});
            if (wide) {
              writer.write_i32(0);
            } else {
              writer.write_i16(0);
            }
          } else if constexpr (std::is_same_v<T, lookup_switch_insn>) {
            auto insn_pos = bytecode.size();
            writer.write_u8(op::lookupswitch);
            auto padding = (4 - (bytecode.size() % 4)) % 4;
            writer.write_u8(static_cast<uint8_t>(padding));
            branches.emplace_back(bytecode.size(), unwritten_branch{arg.default_target, true, insn_pos});
            writer.write_i32(0);
            writer.write_u32(static_cast<uint32_t>(arg.targets.size()));
            for (const auto& [key, target] : arg.targets) {
              writer.write_i32(key);
              branches.emplace_back(bytecode.size(), unwritten_branch{target, true, insn_pos});
              writer.write_i32(0);
            }
          } else if constexpr (std::is_same_v<T, table_switch_insn>) {
            auto insn_pos = bytecode.size();
            writer.write_u8(op::tableswitch);
            auto padding = (4 - (bytecode.size() % 4)) % 4;
            writer.write_u8(static_cast<uint8_t>(padding));
            branches.emplace_back(bytecode.size(), unwritten_branch{arg.default_target, true, insn_pos});
            writer.write_i32(0);
            writer.write_i32(arg.low);
            writer.write_i32(arg.high);
            for (const auto& target : arg.targets) {
              branches.emplace_back(bytecode.size(), unwritten_branch{target, true, insn_pos});
              writer.write_i32(0);
            }
          } else if constexpr (std::is_same_v<T, multi_array_insn>) {
            auto index = ctx_.pool.get_class(arg.descriptor);
            writer.write_u8(op::multianewarray);
            writer.write_u16(index);
            writer.write_u8(arg.dims);
          } else if constexpr (std::is_same_v<T, array_insn>) {
            if (const auto type = std::get_if<std::string>(&arg.type)) {
              auto index = ctx_.pool.get_class(*type);
              writer.write_u8(op::anewarray);
              writer.write_u16(index);
            } else {
              auto array_type = std::get<uint8_t>(arg.type);
              writer.write_u8(op::newarray);
              writer.write_u8(array_type);
            }
          } else if constexpr (std::is_same_v<T, invoke_dynamic_insn>) {
            auto bsm_index = ctx_.get_bsm_index(arg.handle, arg.args);
            auto nat = ctx_.pool.get_name_and_type(arg.name, arg.descriptor);
            uint16_t index{};
            bool found = false;
            for (auto i = 1; i < ctx_.pool.size(); i++) {
              if (const auto id = std::get_if<cp::invoke_dynamic_info>(&ctx_.pool.at(i))) {
                if (id->bootstrap_method_attr_index == bsm_index && id->name_and_type_index == nat) {
                  index = static_cast<uint16_t>(i);
                  found = true;
                  break;
                }
              }
            }
            if (!found) {
              index = static_cast<uint16_t>(ctx_.pool.size());
              ctx_.pool.push_back(cp::invoke_dynamic_info{bsm_index, nat});
            }
            writer.write_u8(op::invokedynamic);
            writer.write_u16(index);
            writer.write_u16(0);
          }
        },
        instr);
  }

  for (const auto& [offset, branch] : branches) {
    size_t target{};
    for (const auto& [l, pos] : labels_) {
      if (l == branch.target) {
        target = pos;
        break;
      }
    }

    if (branch.wide) {
      auto jmp_offset = static_cast<int32_t>(target) - static_cast<int32_t>(branch.instruction_pos);
      bytecode[offset] = static_cast<int8_t>((jmp_offset >> 24) & 0xFF);
      bytecode[offset + 1] = static_cast<int8_t>((jmp_offset >> 16) & 0xFF);
      bytecode[offset + 2] = static_cast<int8_t>((jmp_offset >> 8) & 0xFF);
      bytecode[offset + 3] = static_cast<int8_t>(jmp_offset & 0xFF);
    } else {
      auto jmp_offset = static_cast<int16_t>(target) - static_cast<int16_t>(branch.instruction_pos);
      bytecode[offset] = static_cast<int8_t>((jmp_offset >> 8) & 0xFF);
      bytecode[offset + 1] = static_cast<int8_t>(jmp_offset & 0xFF);
    }
  }
  std::vector<attribute::attribute> attributes;
  if (!code.line_numbers.empty()) {
    ctx_.pool.get_utf("LineNumberTable");
    std::vector<attribute::line_number_table::line_number> table;
    table.reserve(code.line_numbers.size());
    for (const auto& [l, line] : code.line_numbers) {
      table.emplace_back(attribute::line_number_table::line_number{get_label(l), line});
    }
    attributes.emplace_back(attribute::line_number_table{table});
  }
  if (!code.local_vars.empty()) {
    std::vector<attribute::local_variable_table::local_variable> table;
    std::vector<attribute::local_variable_type_table::local_variable> type_table;
    table.reserve(code.local_vars.size());
    type_table.reserve(code.local_vars.size());
    for (const auto& local : code.local_vars) {
      auto name = ctx_.pool.get_utf(local.name);
      auto descriptor = local.descriptor.empty() ? uint16_t{0} : ctx_.pool.get_utf(local.descriptor);
      auto signature = local.signature.empty() ? uint16_t{0} : ctx_.pool.get_utf(local.signature);
      auto start = get_label(local.start);
      auto end = get_label(local.end);
      uint16_t length = end - start;
      if (descriptor != 0) {
        table.emplace_back(
            attribute::local_variable_table::local_variable{start, length, name, descriptor, local.index});
      }
      if (signature != 0) {
        type_table.emplace_back(
            attribute::local_variable_type_table::local_variable{start, length, name, signature, local.index});
      }
    }
    if (!table.empty()) {
      ctx_.pool.get_utf("LocalVariableTable");
      attributes.emplace_back(attribute::local_variable_table{table});
    }
    if (!type_table.empty()) {
      ctx_.pool.get_utf("LocalVariableTypeTable");
      attributes.emplace_back(attribute::local_variable_type_table{type_table});
    }
  }

  if (!code.frames.empty()) {
    ctx_.pool.get_utf("StackMapTable");
    std::vector<attribute::stack_map_frame> frames;
    frames.reserve(code.frames.size());

    auto decompose_stack_var = [this](const frame_var& var) {
      return std::visit(
          [this](const auto& arg) -> attribute::verification_type_info {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, top_var>) {
              return attribute::top_variable_info{};
            } else if constexpr (std::is_same_v<T, int_var>) {
              return attribute::integer_variable_info{};
            } else if constexpr (std::is_same_v<T, float_var>) {
              return attribute::float_variable_info{};
            } else if constexpr (std::is_same_v<T, long_var>) {
              return attribute::long_variable_info{};
            } else if constexpr (std::is_same_v<T, double_var>) {
              return attribute::double_variable_info{};
            } else if constexpr (std::is_same_v<T, null_var>) {
              return attribute::null_variable_info{};
            } else if constexpr (std::is_same_v<T, uninitialized_this_var>) {
              return attribute::uninitialized_this_variable_info{};
            } else if constexpr (std::is_same_v<T, object_var>) {
              auto index = ctx_.pool.get_class(arg.type);
              return attribute::object_variable_info{index};
            } else if constexpr (std::is_same_v<T, uninitalized_var>) {
              uint16_t offset{};
              for (const auto& [l, o] : labels_) {
                if (l == arg.offset) {
                  offset = static_cast<uint16_t>(o);
                  break;
                }
              }
              return attribute::uninitialized_variable_info{offset};
            }
          },
          var);
    };

    uint16_t last_label = 0;
    auto next_delta = [this, &last_label](const label& label) -> uint16_t {
      auto target = get_label(label);
      auto delta = static_cast<uint16_t>(target - last_label);
      if (last_label != 0) {
        delta -= 1;
      }
      last_label = target;
      return delta;
    };
    for (const auto& [label, frame] : code.frames) {
      std::visit(
          [&frames, &label, &decompose_stack_var, &next_delta](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, same_frame>) {
              if (auto stack = arg.stack) {
                auto delta = next_delta(label);
                if (delta <= 63) {
                  frames.emplace_back(attribute::same_locals_1_stack_item_frame{static_cast<uint8_t>(delta + 64),
                                                                                decompose_stack_var(*stack)});
                } else {
                  frames.emplace_back(
                      attribute::same_locals_1_stack_item_frame_extended{delta, decompose_stack_var(*stack)});
                }
              } else {
                auto delta = next_delta(label);
                if (delta <= 63) {
                  frames.emplace_back(attribute::same_frame{static_cast<uint8_t>(delta)});
                } else {
                  frames.emplace_back(attribute::same_frame_extended{delta});
                }
              }
            } else if constexpr (std::is_same_v<T, full_frame>) {
              auto delta = next_delta(label);
              std::vector<attribute::verification_type_info> locals;
              std::vector<attribute::verification_type_info> stack;
              locals.reserve(arg.locals.size());
              stack.reserve(arg.stack.size());
              for (const auto& var : arg.locals) {
                locals.emplace_back(decompose_stack_var(var));
              }
              for (const auto& var : arg.stack) {
                stack.emplace_back(decompose_stack_var(var));
              }
              frames.emplace_back(attribute::full_frame{delta, locals, stack});
            } else if constexpr (std::is_same_v<T, chop_frame>) {
              auto delta = next_delta(label);
              auto frame_type = static_cast<uint8_t>(251 - arg.size);
              frames.emplace_back(attribute::chop_frame{frame_type, delta});
            } else if constexpr (std::is_same_v<T, append_frame>) {
              auto delta = next_delta(label);
              auto frame_type = static_cast<uint8_t>(arg.locals.size() + 251);
              std::vector<attribute::verification_type_info> locals;
              locals.reserve(arg.locals.size());
              for (const auto& var : arg.locals) {
                locals.emplace_back(decompose_stack_var(var));
              }
              frames.emplace_back(attribute::append_frame{frame_type, delta, locals});
            }
          },
          frame);
    }
    attributes.emplace_back(attribute::stack_map_table{frames});
  }

  if (!code.visible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(code.visible_type_annotations.size());
    for (const auto& anno : code.visible_type_annotations) {
      annotations.emplace_back(decompose(code, anno));
    }
    attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!code.invisible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(code.invisible_type_annotations.size());
    for (const auto& anno : code.invisible_type_annotations) {
      annotations.emplace_back(decompose(code, anno));
    }
    attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }

  std::vector<attribute::code::exception> exceptions;
  exceptions.reserve(code.tcb_table.size());
  for (const auto& tcb : code.tcb_table) {
    auto start = get_label(tcb.start);
    auto end = get_label(tcb.end);
    auto handler = get_label(tcb.handler);
    auto catch_type = tcb.type.empty() ? uint16_t{0} : ctx_.pool.get_class(tcb.type);
    exceptions.emplace_back(attribute::code::exception{start, end, handler, catch_type});
  }
  return attribute::code{code.max_stack, code.max_locals, bytecode, exceptions, attributes};
}
attribute::type_annotation code_decomposer::decompose(const code& code, const type_annotation& annotation) {
  auto target_info = std::visit(
      [this, &code](auto&& arg) -> attribute::type_annotation_target {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, target::localvar>) {
          std::vector<attribute::localvar::local> locals;
          locals.reserve(arg.table.size());
          for (const auto& local : arg.table) {
            auto start = get_label(local.start);
            auto end = get_label(local.end);
            auto length = static_cast<uint16_t>(end - start);
            locals.emplace_back(attribute::localvar::local{start, length, local.index});
          }
          return attribute::localvar{locals};
        } else if constexpr (std::is_same_v<T, target::catch_target>) {
          uint16_t index{};
          for (auto i = 0; i < code.tcb_table.size(); i++) {
            if (code.tcb_table[i].handler == arg.handler_label) {
              index = static_cast<uint16_t>(i);
            }
          }
          return attribute::catch_target{index};
        } else if constexpr (std::is_same_v<T, target::offset_target>) {
          auto offset = get_label(arg.offset);
          return attribute::offset_target{offset};
        } else if constexpr (std::is_same_v<T, target::type_argument>) {
          auto offset = get_label(arg.offset);
          return attribute::type_argument{offset, arg.index};
        }
        throw std::runtime_error("Unknown target type");
      },
      annotation.target_info);
  annotation_decomposer annotation_decomposer(ctx_);
  attribute::type_path path;
  path.paths.reserve(annotation.target_path.path.size());
  for (const auto& [kind, index] : annotation.target_path.path) {
    path.paths.emplace_back(attribute::type_path::path{kind, index});
  }
  std::vector<attribute::element_pair> pairs;
  pairs.reserve(annotation.values.size());
  for (const auto& [name, val] : annotation.values) {
    auto name_index = ctx_.pool.get_utf(name);
    auto value = annotation_decomposer.decompose(val);
    pairs.emplace_back(attribute::element_pair{name_index, value});
  }
  auto desc_index = ctx_.pool.get_utf(annotation.descriptor);
  return attribute::type_annotation{annotation.target_type, target_info, path, desc_index, pairs};
}
annotation_decomposer::annotation_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
attribute::annotation annotation_decomposer::decompose(const annotation& annotation) {
  auto desc_index = ctx_.pool.get_utf(annotation.descriptor);
  std::vector<attribute::element_pair> pairs;
  pairs.reserve(annotation.values.size());
  for (const auto& [name, val] : annotation.values) {
    auto name_index = ctx_.pool.get_utf(name);
    auto value = decompose(val);
    pairs.emplace_back(attribute::element_pair{name_index, value});
  }
  return attribute::annotation{desc_index, pairs};
}
attribute::element_value annotation_decomposer::decompose(const element_value& value) {
  return std::visit(
      [this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int8_t>) {
          auto index = ctx_.get_vaule(static_cast<int32_t>(arg));
          return attribute::element_value{'B', index};
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          auto index = ctx_.get_vaule(static_cast<int32_t>(arg));
          return attribute::element_value{'C', index};
        } else if constexpr (std::is_same_v<T, double>) {
          auto index = ctx_.get_vaule(arg);
          return attribute::element_value{'D', index};
        } else if constexpr (std::is_same_v<T, float>) {
          auto index = ctx_.get_vaule(arg);
          return attribute::element_value{'F', index};
        } else if constexpr (std::is_same_v<T, int32_t>) {
          auto index = ctx_.get_vaule(arg);
          return attribute::element_value{'I', index};
        } else if constexpr (std::is_same_v<T, int64_t>) {
          auto index = ctx_.get_vaule(arg);
          return attribute::element_value{'J', index};
        } else if constexpr (std::is_same_v<T, int16_t>) {
          auto index = ctx_.get_vaule(static_cast<int32_t>(arg));
          return attribute::element_value{'S', index};
        } else if constexpr (std::is_same_v<T, bool>) {
          auto index = ctx_.get_vaule(arg ? 1 : 0);
          return attribute::element_value{'Z', index};
        } else if constexpr (std::is_same_v<T, std::string>) {
          auto index = ctx_.pool.get_utf(arg);
          return attribute::element_value{'s', index};
        } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
          auto type_name_index = ctx_.pool.get_utf(arg.first);
          auto const_name_index = ctx_.pool.get_utf(arg.second);
          return attribute::element_value{'e', attribute::element_value::enum_value{type_name_index, const_name_index}};
        } else if constexpr (std::is_same_v<T, class_value>) {
          auto index = ctx_.pool.get_utf(arg.get());
          return attribute::element_value{'c', index};
        } else if constexpr (std::is_same_v<T, annotation>) {
          auto anno = decompose(arg);
          return attribute::element_value{'@', anno};
        } else if constexpr (std::is_same_v<T, std::vector<element_value>>) {
          std::vector<attribute::element_value> values;
          values.reserve(arg.size());
          for (const auto& e : arg) {
            values.emplace_back(decompose(e));
          }
          return attribute::element_value{'[', values};
        }
      },
      value.value);
}
record_decomposer::record_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
attribute::record::component record_decomposer::decompose(const record_component& component) {
  auto name_index = ctx_.pool.get_utf(component.name);
  auto descriptor_index = ctx_.pool.get_utf(component.descriptor);
  std::vector<attribute::attribute> attributes;
  if (!component.signature.empty()) {
    ctx_.pool.get_utf("Signature");
    attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(component.signature)});
  }
  if (!component.visible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(component.visible_annotations.size());
    for (const auto& anno : component.visible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!component.invisible_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleAnnotations");
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(component.invisible_annotations.size());
    for (const auto& anno : component.invisible_annotations) {
      annotations.emplace_back(annotation_decomposer.decompose(anno));
    }
    attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!component.visible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeVisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(component.visible_type_annotations.size());
    for (const auto& anno : component.visible_type_annotations) {
      annotation_decomposer annotation_decomposer(ctx_);
      attribute::type_path path;
      path.paths.reserve(anno.target_path.path.size());
      for (const auto& [kind, index] : anno.target_path.path) {
        path.paths.emplace_back(attribute::type_path::path{kind, index});
      }
      std::vector<attribute::element_pair> pairs;
      pairs.reserve(anno.values.size());
      for (const auto& [name, val] : anno.values) {
        auto name_index = ctx_.pool.get_utf(name);
        auto value = annotation_decomposer.decompose(val);
        pairs.emplace_back(attribute::element_pair{name_index, value});
      }
      auto desc_index = ctx_.pool.get_utf(anno.descriptor);
      annotations.emplace_back(
          attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!component.invisible_type_annotations.empty()) {
    ctx_.pool.get_utf("RuntimeInvisibleTypeAnnotations");
    std::vector<attribute::type_annotation> annotations;
    annotations.reserve(component.invisible_type_annotations.size());
    for (const auto& anno : component.invisible_type_annotations) {
      annotation_decomposer annotation_decomposer(ctx_);
      attribute::type_path path;
      path.paths.reserve(anno.target_path.path.size());
      for (const auto& [kind, index] : anno.target_path.path) {
        path.paths.emplace_back(attribute::type_path::path{kind, index});
      }
      std::vector<attribute::element_pair> pairs;
      pairs.reserve(anno.values.size());
      for (const auto& [name, val] : anno.values) {
        auto name_index = ctx_.pool.get_utf(name);
        auto value = annotation_decomposer.decompose(val);
        pairs.emplace_back(attribute::element_pair{name_index, value});
      }
      auto desc_index = ctx_.pool.get_utf(anno.descriptor);
      annotations.emplace_back(
          attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }
  return attribute::record::component{name_index, descriptor_index, attributes};
}
} // namespace cafe
