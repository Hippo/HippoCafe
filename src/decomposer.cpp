#include "decomposer.hpp"

namespace cafe {
decomposer_ctx::decomposer_ctx(data_writer& writer) : writer(writer) {
}
uint16_t decomposer_ctx::get_vaule(const value& value) {
  return pool.get_value(bsm_buffer, value);
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
    cf.interfaces.push_back(ctx_.pool.get_class(interface));
  }
  cf.fields.reserve(class_model.fields.size());
  field_decomposer field_decomposer(ctx_);
  for (const auto& field : class_model.fields) {
    cf.fields.push_back(field_decomposer.decompose(field));
  }
  cf.methods.reserve(class_model.methods.size());
  method_decomposer method_decomposer(ctx_);
  for (const auto& method : class_model.methods) {
    cf.methods.push_back(method_decomposer.decompose(method));
  }

  if (class_model.synthetic) {
    cf.attributes.emplace_back(attribute::synthetic{});
  }
  if (class_model.deprecated) {
    cf.attributes.emplace_back(attribute::deprecated{});
  }
  if (!class_model.signature.empty()) {
    cf.attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(class_model.signature)});
  }
  if (!class_model.source_file.empty()) {
    cf.attributes.emplace_back(attribute::source_file{ctx_.pool.get_utf(class_model.source_file)});
  }
  if (!class_model.inner_classes.empty()) {
    std::vector<attribute::inner_classes::inner_class> inner_classes;
    inner_classes.reserve(class_model.inner_classes.size());
    for (const auto& inner : class_model.inner_classes) {
      inner_classes.emplace_back(attribute::inner_classes::inner_class{ctx_.pool.get_class(inner.name), ctx_.pool.get_class(inner.outer_name), ctx_.pool.get_utf(inner.inner_name), inner.access_flags});
    }
    cf.attributes.emplace_back(attribute::inner_classes{inner_classes});
  }

  if (!class_model.enclosing_owner.empty() && !class_model.enclosing_name.empty() && !class_model.enclosing_descriptor.empty()) {
    auto owner_index = ctx_.pool.get_class(class_model.enclosing_owner);
    auto name_and_type_index = ctx_.pool.get_name_and_type(class_model.enclosing_name, class_model.enclosing_descriptor);
    cf.attributes.emplace_back(attribute::enclosing_method{owner_index, name_and_type_index});
  }

  if (!class_model.source_debug_extension.empty()) {
    std::vector<uint8_t> vec(class_model.source_debug_extension.begin(), class_model.source_debug_extension.end());
    cf.attributes.emplace_back(attribute::source_debug_extension{vec});
  }

  if (class_model.module) {
    auto name_index = ctx_.pool.get_utf(class_model.module->name);
    auto version_index = ctx_.pool.get_utf(class_model.module->version);
    std::vector<attribute::module::require> requires;
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
    cf.attributes.emplace_back(attribute::module{name_index, class_model.module->access_flags, version_index, requires, exports, opens, uses, provides});
  }

  if (!class_model.module_packages.empty()) {
    std::vector<uint16_t> package_indices;
    package_indices.reserve(class_model.module_packages.size());
    for (const auto& package : class_model.module_packages) {
      package_indices.emplace_back(ctx_.pool.get_package(package));
    }
    cf.attributes.emplace_back(attribute::module_packages{package_indices});
  }

  if (!class_model.module_main_class.empty()) {
    auto index = ctx_.pool.get_class(class_model.module_main_class);
    cf.attributes.emplace_back(attribute::module_main_class{index});
  }

  if (!class_model.nest_host.empty()) {
    auto index = ctx_.pool.get_class(class_model.nest_host);
    cf.attributes.emplace_back(attribute::nest_host{index});
  }

  if (!class_model.nest_members.empty()) {
    std::vector<uint16_t> indices;
    indices.reserve(class_model.nest_members.size());
    for (const auto& member : class_model.nest_members) {
      indices.emplace_back(ctx_.pool.get_class(member));
    }
    cf.attributes.emplace_back(attribute::nest_members{indices});
  }

  if (!class_model.permitted_subclasses.empty()) {
    std::vector<uint16_t> indices;
    indices.reserve(class_model.permitted_subclasses.size());
    for (const auto& subclass : class_model.permitted_subclasses) {
      indices.emplace_back(ctx_.pool.get_class(subclass));
    }
    cf.attributes.emplace_back(attribute::permitted_subclasses{indices});
  }

  if (!class_model.record_components.empty()) {
    std::vector<attribute::record::component> components;
    components.reserve(class_model.record_components.size());
    record_decomposer record_decomposer(ctx_);
    for (const auto& component : class_model.record_components) {
      components.push_back(record_decomposer.decompose(component));
    }
    cf.attributes.emplace_back(attribute::record{components});
  }

  if (!class_model.visible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(class_model.visible_annotations.size());
    for (const auto& anno : class_model.visible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    cf.attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!class_model.invisible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(class_model.invisible_annotations.size());
    for (const auto& anno : class_model.invisible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    cf.attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }

  return cf;
}
field_decomposer::field_decomposer(decomposer_ctx& ctx) : ctx_(ctx) {
}
field_info field_decomposer::decompose(const field_model& field_model) {
  field_info field;
  field.access_flags = field_model.access_flags;
  field.name_index = ctx_.pool.get_utf(field_model.name);
  field.descriptor_index = ctx_.pool.get_utf(field_model.descriptor);
  if (field_model.synthetic) {
    field.attributes.emplace_back(attribute::synthetic{});
  }
  if (field_model.deprecated) {
    field.attributes.emplace_back(attribute::deprecated{});
  }
  if (field_model.constant_value) {
    field.attributes.emplace_back(attribute::constant_value{ctx_.pool.get_value(ctx_.bsm_buffer, *field_model.constant_value)});
  }
  if (!field_model.signature.empty()) {
    field.attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(field_model.signature)});
  }
  if (!field_model.visible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(field_model.visible_annotations.size());
    for (const auto& anno : field_model.visible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    field.attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!field_model.invisible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(field_model.invisible_annotations.size());
    for (const auto& anno : field_model.invisible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    field.attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!field_model.visible_type_annotations.empty()) {
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
      annotations.emplace_back(attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    field.attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!field_model.invisible_type_annotations.empty()) {
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
      annotations.emplace_back(attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
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
  return method;
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
            values.push_back(decompose(e));
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
    attributes.emplace_back(attribute::signature{ctx_.pool.get_utf(component.signature)});
  }
  if (!component.visible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(component.visible_annotations.size());
    for (const auto& anno : component.visible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    attributes.emplace_back(attribute::runtime_visible_annotations{annotations});
  }
  if (!component.invisible_annotations.empty()) {
    annotation_decomposer annotation_decomposer(ctx_);
    std::vector<attribute::annotation> annotations;
    annotations.reserve(component.invisible_annotations.size());
    for (const auto& anno : component.invisible_annotations) {
      annotations.push_back(annotation_decomposer.decompose(anno));
    }
    attributes.emplace_back(attribute::runtime_invisible_annotations{annotations});
  }
  if (!component.visible_type_annotations.empty()) {
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
      annotations.emplace_back(attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    attributes.emplace_back(attribute::runtime_visible_type_annotations{annotations});
  }
  if (!component.invisible_type_annotations.empty()) {
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
      annotations.emplace_back(attribute::type_annotation{anno.target_type, attribute::empty{}, path, desc_index, pairs});
    }
    attributes.emplace_back(attribute::runtime_invisible_type_annotations{annotations});
  }
  return attribute::record::component{name_index, descriptor_index, attributes};
}
} // namespace cafe
