#include "composer.hpp"

#include "cafe/constants.hpp"

namespace cafe {

static std::optional<const attribute::bootstrap_methods*> get_bsms(const class_file& cf) {
  for (const auto& attr : cf.attributes) {
    if (const auto bm = std::get_if<attribute::bootstrap_methods>(&attr)) {
      return bm;
    }
  }
  return std::nullopt;
}

ref::ref(const std::string_view& owner, const std::string_view& name, const std::string_view& descriptor) :
    owner(owner), name(name), descriptor(descriptor) {
}
ref ref::from(const class_file& cf, uint16_t index) {
  return std::visit(
      [&cf](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, cp::field_ref_info> || std::is_same_v<T, cp::method_ref_info> ||
                      std::is_same_v<T, cp::interface_method_ref_info>) {
          auto& nat = std::get<cp::name_and_type_info>(cf.constant_pool.at(arg.name_and_type_index));
          return ref(cf.constant_pool.get_string(arg.class_index), cf.constant_pool.get_string(nat.name_index),
                     cf.constant_pool.get_string(nat.descriptor_index));
        } else {
          return ref();
        }
      },
      cf.constant_pool.at(index));
}
value get_value(const class_file& cf, uint16_t index) {
  if (const auto int_info = std::get_if<cp::integer_info>(&cf.constant_pool.at(index))) {
    return int_info->value;
  }
  if (const auto float_info = std::get_if<cp::float_info>(&cf.constant_pool.at(index))) {
    return float_info->value;
  }
  if (const auto long_info = std::get_if<cp::long_info>(&cf.constant_pool.at(index))) {
    return long_info->value;
  }
  if (const auto double_info = std::get_if<cp::double_info>(&cf.constant_pool.at(index))) {
    return double_info->value;
  }
  if (const auto class_info = std::get_if<cp::class_info>(&cf.constant_pool.at(index))) {
    return class_value(cf.constant_pool.get_string(class_info->name_index));
  }
  if (const auto string_info = std::get_if<cp::string_info>(&cf.constant_pool.at(index))) {
    return cf.constant_pool.get_string(string_info->string_index);
  }
  if (const auto handle = std::get_if<cp::method_handle_info>(&cf.constant_pool.at(index))) {
    auto reference = ref::from(cf, handle->reference_index);
    return method_handle(handle->reference_kind, reference.owner, reference.name, reference.descriptor);
  }
  if (const auto method_type = std::get_if<cp::method_type_info>(&cf.constant_pool.at(index))) {
    return cafe::method_type(cf.constant_pool.get_string(method_type->descriptor_index));
  }
  if (const auto dynamic = std::get_if<cp::dynamic_info>(&cf.constant_pool.at(index))) {
    auto bsms = get_bsms(cf);
    if (!bsms) {
      throw std::runtime_error("No bootstrap methods attribute found");
    }
    auto& bsm = (*bsms)->methods[dynamic->bootstrap_method_attr_index];
    auto& mh = std::get<cp::method_handle_info>(cf.constant_pool.at(bsm.index));
    auto reference = ref::from(cf, mh.reference_index);
    std::vector<value> args;
    args.reserve(bsm.arguments.size());
    for (const auto& arg : bsm.arguments) {
      args.emplace_back(get_value(cf, arg));
    }
    auto& nat = std::get<cp::name_and_type_info>(cf.constant_pool.at(dynamic->name_and_type_index));
    return cafe::dynamic(cf.constant_pool.get_string(nat.name_index), cf.constant_pool.get_string(nat.descriptor_index),
                         method_handle(mh.reference_kind, reference.owner, reference.name, reference.descriptor), args);
  }
  return 0;
}

composer::composer(const class_file& cf, const std::function<void(const std::exception&)>& error_handler) :
    cf_(cf), error_handler_(error_handler) {
}
annotation composer::compose_annotation(const attribute::annotation& anno) {
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  annotation an(type);
  an.values.reserve(anno.elements.size());
  for (const auto& [name_index, value] : anno.elements) {
    const auto ev = compose_element_value(value);
    an.values.emplace(cf_.constant_pool.get_string(name_index), ev);
  }
  return an;
}
element_value composer::compose_element_value(const attribute::element_value& value) {
  element_value ev;
  switch (value.tag) {
    case 'B':
      ev.value =
          static_cast<int8_t>(std::get<cp::integer_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value);
      break;
    case 'C':
      ev.value =
          static_cast<uint16_t>(std::get<cp::integer_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value);
      break;
    case 'D':
      ev.value = std::get<cp::double_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value;
      break;
    case 'F':
      ev.value = std::get<cp::float_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value;
      break;
    case 'I':
      ev.value = std::get<cp::integer_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value;
      break;
    case 'J':
      ev.value = std::get<cp::long_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value;
      break;
    case 'S':
      ev.value =
          static_cast<int16_t>(std::get<cp::integer_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value);
      break;
    case 'Z':
      ev.value = std::get<cp::integer_info>(cf_.constant_pool[std::get<uint16_t>(value.value)]).value != 0;
      break;
    case 's':
      ev.value = cf_.constant_pool.get_string(std::get<uint16_t>(value.value));
      break;
    case 'e':
      auto enum_info = std::get<attribute::element_value::enum_value>(value.value);
      ev.value = std::make_pair(cf_.constant_pool.get_string(enum_info.type_name_index),
                                cf_.constant_pool.get_string(enum_info.const_name_index));
      break;
    case 'c':
      ev.value = class_value(cf_.constant_pool.get_string(std::get<uint16_t>(value.value)));
      break;
    case '@':
      ev.value = compose_annotation(std::get<attribute::annotation>(value.value));
      break;
    case '[': {
      const auto& values = std::get<std::vector<attribute::element_value>>(value.value);
      std::vector<element_value> elements;
      elements.reserve(values.size());
      for (const auto& v : values) {
        elements.emplace_back(compose_element_value(v));
      }
      ev.value = std::move(elements);
      break;
    }
    default:
      throw std::runtime_error("Unknown element value tag");
      break;
  }
  return ev;
}
class_composer::class_composer(const class_file& cf, const std::function<void(const std::exception&)>& error_handler) :
    composer(cf, error_handler) {
}
type_annotation class_composer::compose_type_annotation(const attribute::type_annotation& anno) {
  auto target = std::visit(
      [this](auto&& arg) -> type_annotation_target {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, attribute::type_parameter>) {
          return target::type_parameter(arg.index);
        }
        if constexpr (std::is_same_v<T, attribute::supertype>) {
          return target::supertype(arg.index == 65535 ? cf_.constant_pool.get_string(cf_.super_class)
                                                      : cf_.constant_pool.get_string(cf_.interfaces[arg.index]));
        }
        if constexpr (std::is_same_v<T, attribute::type_parameter_bound>) {
          return target::type_parameter_bound(arg.type_parameter_index, arg.bound_index);
        }
        throw std::runtime_error("Unknown type annotation target");
      },
      anno.target_info);
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(anno.target_path.paths.size());
  for (const auto& [kind, index] : anno.target_path.paths) {
    path.emplace_back(kind, index);
  }
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  type_annotation an(anno.target_type, target, type_path(path), type);
  an.values.reserve(anno.elements.size());
  for (const auto& pair : anno.elements) {
    const auto ev = compose_element_value(pair.value);
    an.values.emplace(cf_.constant_pool.get_string(pair.name_index), ev);
  }
  return an;
}
void class_composer::compose(class_model& model) {
  try {
    model.version = static_cast<uint32_t>(cf_.major_version) << 16 | cf_.minor_version;
    model.access_flags = cf_.access_flags;
    model.name = cf_.constant_pool.get_string(cf_.this_class);
    model.super_name = cf_.constant_pool.get_string(cf_.super_class);
    model.interfaces.reserve(cf_.interfaces.size());
    for (const auto& index : cf_.interfaces) {
      model.interfaces.push_back(cf_.constant_pool.get_string(index));
    }
    model.fields.reserve(cf_.fields.size());
    for (const auto& field : cf_.fields) {
      field_composer fc(cf_, field, error_handler_);
      model.fields.emplace_back(fc.compose());
    }
    model.methods.reserve(cf_.methods.size());
    for (const auto& method : cf_.methods) {
      method_composer mc(cf_, method, error_handler_);
      model.methods.emplace_back(mc.compose());
    }
    for (const auto& attr : cf_.attributes) {
      std::visit(
          [this, &model](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, attribute::signature>) {
              model.signature = cf_.constant_pool.get_string(arg.index);
            } else if constexpr (std::is_same_v<T, attribute::runtime_visible_annotations>) {
              model.visible_annotations.reserve(arg.annotations.size());
              for (const auto& anno : arg.annotations) {
                try {
                  model.visible_annotations.emplace_back(compose_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
            } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_annotations>) {
              model.invisible_annotations.reserve(arg.annotations.size());
              for (const auto& anno : arg.annotations) {
                try {
                  model.invisible_annotations.emplace_back(compose_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
            } else if constexpr (std::is_same_v<T, attribute::runtime_visible_type_annotations>) {
              model.visible_type_annotations.reserve(arg.annotations.size());
              for (const auto& anno : arg.annotations) {
                try {
                  model.visible_type_annotations.emplace_back(compose_type_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
            } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_type_annotations>) {
              model.invisible_type_annotations.reserve(arg.annotations.size());
              for (const auto& anno : arg.annotations) {
                try {
                  model.invisible_type_annotations.emplace_back(compose_type_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
            } else if constexpr (std::is_same_v<T, attribute::synthetic>) {
              model.synthetic = true;
            } else if constexpr (std::is_same_v<T, attribute::deprecated>) {
              model.deprecated = true;
            } else if constexpr (std::is_same_v<T, attribute::source_file>) {
              model.source_file = cf_.constant_pool.get_string(arg.index);
            } else if constexpr (std::is_same_v<T, attribute::source_debug_extension>) {
              model.source_debug_extension = std::string(arg.debug_extension.begin(), arg.debug_extension.end());
            } else if constexpr (std::is_same_v<T, attribute::inner_classes>) {
              model.inner_classes.reserve(arg.classes.size());
              for (const auto& in : arg.classes) {
                model.inner_classes.emplace_back(inner_class(
                    cf_.constant_pool.get_string(in.inner_index), cf_.constant_pool.get_string(in.outer_index),
                    cf_.constant_pool.get_string(in.name_index), in.access_flags));
              }
            } else if constexpr (std::is_same_v<T, attribute::enclosing_method>) {
              auto& nat = std::get<cp::name_and_type_info>(cf_.constant_pool[arg.method_index]);
              model.enclosing_owner = cf_.constant_pool.get_string(arg.class_index);
              model.enclosing_name = cf_.constant_pool.get_string(nat.name_index);
              model.enclosing_descriptor = cf_.constant_pool.get_string(nat.descriptor_index);
            } else if constexpr (std::is_same_v<T, attribute::module>) {
              module_model mm(cf_.constant_pool.get_string(arg.module_name_index), arg.module_flags,
                              cf_.constant_pool.get_string(arg.module_version_index));
              mm.
                requires
                  .reserve(arg.requires.size());
              for (const auto& req : arg.requires) {
                mm.
                  requires
                    .emplace_back(cf_.constant_pool.get_string(req.require_index), req.require_flags,
                                  cf_.constant_pool.get_string(req.require_version_index));
              }
              mm.exports.reserve(arg.exports.size());
              for (const auto& ex : arg.exports) {
                std::vector<std::string> modules;
                modules.reserve(ex.exports_to_index.size());
                for (const auto& index : ex.exports_to_index) {
                  modules.emplace_back(cf_.constant_pool.get_string(index));
                }
                mm.exports.emplace_back(cf_.constant_pool.get_string(ex.exports_index), ex.exports_flags,
                                        std::move(modules));
              }
              mm.opens.reserve(arg.opens.size());
              for (const auto& op : arg.opens) {
                std::vector<std::string> modules;
                modules.reserve(op.opens_to_index.size());
                for (const auto& index : op.opens_to_index) {
                  modules.emplace_back(cf_.constant_pool.get_string(index));
                }
                mm.opens.emplace_back(cf_.constant_pool.get_string(op.opens_index), op.opens_flags, std::move(modules));
              }
              mm.uses.reserve(arg.uses.size());
              for (const auto& index : arg.uses) {
                mm.uses.emplace_back(cf_.constant_pool.get_string(index));
              }
              mm.provides.reserve(arg.provides.size());
              for (const auto& pro : arg.provides) {
                std::vector<std::string> providers;
                providers.reserve(pro.provides_with_index.size());
                for (const auto& index : pro.provides_with_index) {
                  providers.emplace_back(cf_.constant_pool.get_string(index));
                }
                mm.provides.emplace_back(cf_.constant_pool.get_string(pro.provides_index), std::move(providers));
              }
              model.module = std::move(mm);
            } else if constexpr (std::is_same_v<T, attribute::module_packages>) {
              model.module_packages.reserve(arg.package_indices.size());
              for (const auto& index : arg.package_indices) {
                model.module_packages.emplace_back(cf_.constant_pool.get_string(index));
              }
            } else if constexpr (std::is_same_v<T, attribute::module_main_class>) {
              model.module_main_class = cf_.constant_pool.get_string(arg.index);
            } else if constexpr (std::is_same_v<T, attribute::nest_host>) {
              model.nest_host = cf_.constant_pool.get_string(arg.index);
            } else if constexpr (std::is_same_v<T, attribute::nest_members>) {
              model.nest_members.reserve(arg.classes.size());
              for (const auto& index : arg.classes) {
                model.nest_members.emplace_back(cf_.constant_pool.get_string(index));
              }
            } else if constexpr (std::is_same_v<T, attribute::permitted_subclasses>) {
              model.permitted_subclasses.reserve(arg.classes.size());
              for (const auto& index : arg.classes) {
                model.permitted_subclasses.emplace_back(cf_.constant_pool.get_string(index));
              }
            } else if constexpr (std::is_same_v<T, attribute::record>) {
              model.record_components.reserve(arg.components.size());
              for (const auto& comp : arg.components) {
                record_composer rc(cf_, comp, error_handler_);
                model.record_components.emplace_back(rc.compose());
              }
            }
          },
          attr);
    }
  } catch (const std::exception& e) {
    error_handler_(e);
  }
}
field_composer::field_composer(const class_file& cf, const field_info& field,
                               const std::function<void(const std::exception&)>& error_handler) :
    composer(cf, error_handler), field_(field) {
}
type_annotation field_composer::compose_type_annotation(const attribute::type_annotation& anno) {
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(anno.target_path.paths.size());
  for (const auto& [kind, index] : anno.target_path.paths) {
    path.emplace_back(kind, index);
  }
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  type_annotation an(anno.target_type, target::empty(), type_path(path), type);
  an.values.reserve(anno.elements.size());
  for (const auto& pair : anno.elements) {
    const auto ev = compose_element_value(pair.value);
    an.values.emplace(cf_.constant_pool.get_string(pair.name_index), ev);
  }
  return an;
}
field_model field_composer::compose() {
  field_model fm(field_.access_flags, cf_.constant_pool.get_string(field_.name_index),
                 cf_.constant_pool.get_string(field_.descriptor_index));
  for (const auto& attr : field_.attributes) {
    std::visit(
        [this, &fm](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, attribute::synthetic>) {
            fm.synthetic = true;
          } else if constexpr (std::is_same_v<T, attribute::deprecated>) {
            fm.deprecated = true;
          } else if constexpr (std::is_same_v<T, attribute::constant_value>) {
            if (const auto& cp_info = cf_.constant_pool[arg.index]; std::holds_alternative<cp::integer_info>(cp_info)) {
              fm.constant_value = std::get<cp::integer_info>(cp_info).value;
            } else if (std::holds_alternative<cp::float_info>(cp_info)) {
              fm.constant_value = std::get<cp::float_info>(cp_info).value;
            } else if (std::holds_alternative<cp::long_info>(cp_info)) {
              fm.constant_value = std::get<cp::long_info>(cp_info).value;
            } else if (std::holds_alternative<cp::double_info>(cp_info)) {
              fm.constant_value = std::get<cp::double_info>(cp_info).value;
            } else if (std::holds_alternative<cp::string_info>(cp_info)) {
              fm.constant_value = std::get<cp::string_info>(cp_info).string_index;
            }
          } else if constexpr (std::is_same_v<T, attribute::signature>) {
            fm.signature = cf_.constant_pool.get_string(arg.index);
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_annotations>) {
            fm.visible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                fm.visible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_annotations>) {
            fm.invisible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                fm.invisible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_type_annotations>) {
            fm.visible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                fm.visible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_type_annotations>) {
            fm.invisible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                fm.invisible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          }
        },
        attr);
  }
  return fm;
}

method_composer::method_composer(const class_file& cf, const method_info& method,
                                 const std::function<void(const std::exception&)>& error_handler) :
    composer(cf, error_handler), method_(method) {
}
type_annotation method_composer::compose_type_annotation(const attribute::type_annotation& anno) {
  auto target = std::visit(
      [this](auto&& arg) -> type_annotation_target {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, attribute::type_parameter>) {
          return target::type_parameter(arg.index);
        }
        if constexpr (std::is_same_v<T, attribute::type_parameter_bound>) {
          return target::type_parameter_bound(arg.type_parameter_index, arg.bound_index);
        }
        if constexpr (std::is_same_v<T, attribute::empty>) {
          return target::empty();
        }
        if constexpr (std::is_same_v<T, attribute::formal_parameter>) {
          return target::formal_parameter(arg.index);
        }
        if constexpr (std::is_same_v<T, attribute::throws>) {
          return target::throws(exceptions_[arg.index]);
        }
        throw std::runtime_error("Unknown type annotation target");
      },
      anno.target_info);

  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(anno.target_path.paths.size());
  for (const auto& [kind, index] : anno.target_path.paths) {
    path.emplace_back(kind, index);
  }
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  type_annotation an(anno.target_type, target, type_path(path), type);
  an.values.reserve(anno.elements.size());
  for (const auto& pair : anno.elements) {
    const auto ev = compose_element_value(pair.value);
    an.values.emplace(cf_.constant_pool.get_string(pair.name_index), ev);
  }
  return an;
}
method_model method_composer::compose() {
  method_model mm(method_.access_flags, cf_.constant_pool.get_string(method_.name_index),
                  cf_.constant_pool.get_string(method_.descriptor_index));


  for (const auto& attr : method_.attributes) {
    if (auto* ex = std::get_if<attribute::exceptions>(&attr)) {
      exceptions_.reserve(ex->exception_index_table.size());
      for (const auto& index : ex->exception_index_table) {
        exceptions_.push_back(cf_.constant_pool.get_string(index));
      }
      mm.exceptions = exceptions_;
      break;
    }
  }


  for (const auto& attr : method_.attributes) {
    std::visit(
        [this, &mm](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, attribute::signature>) {
            mm.signature = cf_.constant_pool.get_string(arg.index);
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_annotations>) {
            mm.visible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                mm.visible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_annotations>) {
            mm.invisible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                mm.invisible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_type_annotations>) {
            mm.visible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                mm.visible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_type_annotations>) {
            mm.invisible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                mm.invisible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::annotation_default>) {
            try {
              mm.annotation_default = compose_element_value(arg.default_value);
            } catch (const std::exception& e) {
              error_handler_(e);
            }
          } else if constexpr (std::is_same_v<T, attribute::synthetic>) {
            mm.synthetic = true;
          } else if constexpr (std::is_same_v<T, attribute::deprecated>) {
            mm.deprecated = true;
          } else if constexpr (std::is_same_v<T, attribute::method_parameters>) {
            mm.method_parameters.reserve(arg.parameters.size());
            for (const auto& param : arg.parameters) {
              mm.method_parameters.emplace_back(param.access_flags, cf_.constant_pool.get_string(param.name_index));
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_parameter_annotations>) {
            mm.visible_parameter_annotations.reserve(arg.parameter_annotations.size());
            for (const auto& annos : arg.parameter_annotations) {
              std::vector<annotation> annotations;
              annotations.reserve(annos.size());
              for (const auto& anno : annos) {
                try {
                  annotations.emplace_back(compose_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
              mm.visible_parameter_annotations.emplace_back(std::move(annotations));
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_parameter_annotations>) {
            mm.invisible_parameter_annotations.reserve(arg.parameter_annotations.size());
            for (const auto& annos : arg.parameter_annotations) {
              std::vector<annotation> annotations;
              annotations.reserve(annos.size());
              for (const auto& anno : annos) {
                try {
                  annotations.emplace_back(compose_annotation(anno));
                } catch (const std::exception& e) {
                  error_handler_(e);
                }
              }
              mm.invisible_parameter_annotations.emplace_back(std::move(annotations));
            }
          } else if constexpr (std::is_same_v<T, attribute::code>) {
            code_composer cc(cf_, *this, arg, error_handler_);
            mm.code = cc.compose();
          }
        },
        attr);
  }

  return mm;
}

record_composer::record_composer(const class_file& cf, const attribute::record::component& component,
                                 const std::function<void(const std::exception&)>& error_handler) :
    composer(cf, error_handler), component_(component) {
}
type_annotation record_composer::compose_type_annotation(const attribute::type_annotation& anno) {
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(anno.target_path.paths.size());
  for (const auto& [kind, index] : anno.target_path.paths) {
    path.emplace_back(kind, index);
  }
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  type_annotation an(anno.target_type, target::empty(), type_path(path), type);
  an.values.reserve(anno.elements.size());
  for (const auto& pair : anno.elements) {
    const auto ev = compose_element_value(pair.value);
    an.values.emplace(cf_.constant_pool.get_string(pair.name_index), ev);
  }
  return an;
}
record_component record_composer::compose() {
  record_component rc(cf_.constant_pool.get_string(component_.name_index),
                      cf_.constant_pool.get_string(component_.descriptor_index));
  for (const auto& attr : component_.attributes) {
    std::visit(
        [this, &rc](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, attribute::signature>) {
            rc.signature = cf_.constant_pool.get_string(arg.index);
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_annotations>) {
            rc.visible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                rc.visible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_annotations>) {
            rc.invisible_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                rc.invisible_annotations.emplace_back(compose_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_type_annotations>) {
            rc.visible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                rc.visible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_type_annotations>) {
            rc.invisible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                rc.invisible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          }
        },
        attr);
  }
  return rc;
}

code_composer::code_composer(const class_file& cf, const method_composer& method, const attribute::code& code_attr,
                             const std::function<void(const std::exception&)>& error_handler) :
    composer(cf, error_handler), method_composer_(method), code_attr_(code_attr) {
}
type_annotation code_composer::compose_type_annotation(const attribute::type_annotation& anno) {
  auto target = std::visit(
      [this](auto&& args) -> type_annotation_target {
        using T = std::decay_t<decltype(args)>;
        if constexpr (std::is_same_v<T, attribute::localvar>) {
          std::vector<target::local> locals;
          locals.reserve(args.table.size());
          for (const auto& local : args.table) {
            auto end_pc = local.start_pc + local.length;
            label start = get_label(local.start_pc);
            label end = get_label(end_pc);
            locals.emplace_back(start, end, local.index);
          }
          return target::localvar(locals);
        } else if constexpr (std::is_same_v<T, attribute::catch_target>) {
          auto& ex = code_attr_.exceptions[args.index];
          label handler = get_label(ex.handler_pc);
          return target::catch_target(handler);
        } else if constexpr (std::is_same_v<T, attribute::offset_target>) {
          label offset = get_label(args.offset);
          return target::offset_target(offset);
        } else if constexpr (std::is_same_v<T, attribute::type_argument>) {
          label offset = get_label(args.offset);
          return target::type_argument(offset, args.index);
        }
        throw std::runtime_error("Unknown type annotation target");
      },
      anno.target_info);
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(anno.target_path.paths.size());
  for (const auto& [kind, index] : anno.target_path.paths) {
    path.emplace_back(kind, index);
  }
  const auto& type = cf_.constant_pool.get_string(anno.type_index);
  type_annotation an(anno.target_type, target, type_path(path), type);
  an.values.reserve(anno.elements.size());
  for (const auto& pair : anno.elements) {
    const auto ev = compose_element_value(pair.value);
    an.values.emplace(cf_.constant_pool.get_string(pair.name_index), ev);
  }
  return an;
}


code code_composer::compose() {
  code c;
  c.max_locals = code_attr_.max_locals;
  c.max_stack = code_attr_.max_stack;
  c.tcb_table.reserve(code_attr_.exceptions.size());
  for (const auto& ex : code_attr_.exceptions) {
    auto start = get_label(ex.start_pc);
    auto end = get_label(ex.end_pc);
    auto handler = get_label(ex.handler_pc);
    c.tcb_table.emplace_back(start, end, handler, cf_.constant_pool.get_string(ex.catch_type));
  }

  for (const auto& attr : code_attr_.attributes) {
    std::visit(
        [this, &c](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, attribute::line_number_table>) {
            c.line_numbers.reserve(arg.line_numbers.size());
            for (const auto& ln : arg.line_numbers) {
              c.line_numbers.emplace_back(get_label(ln.start_pc), ln.number);
            }
          } else if constexpr (std::is_same_v<T, attribute::local_variable_table>) {
            for (const auto& local : arg.local_variables) {
              auto start = get_label(local.start_pc);
              bool found = false;
              for (auto& current : c.local_vars) {
                if (current.start == start && current.index == local.index) {
                  current.descriptor = cf_.constant_pool.get_string(local.descriptor_index);
                  found = true;
                  break;
                }
              }
              if (!found) {
                c.local_vars.emplace_back(cf_.constant_pool.get_string(local.name_index),
                                          cf_.constant_pool.get_string(local.descriptor_index), "", start,
                                          get_label(local.start_pc + local.length), local.index);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::local_variable_type_table>) {
            for (const auto& local : arg.local_variables) {
              auto start = get_label(local.start_pc);
              bool found = false;
              for (auto& current : c.local_vars) {
                if (current.start == start && current.index == local.index) {
                  current.signature = cf_.constant_pool.get_string(local.signature_index);
                  found = true;
                  break;
                }
              }
              if (!found) {
                c.local_vars.emplace_back(cf_.constant_pool.get_string(local.name_index), "",
                                          cf_.constant_pool.get_string(local.signature_index), start,
                                          get_label(local.start_pc + local.length), local.index);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::stack_map_table>) {
            uint32_t offset_delta = 0;
            for (const auto& entry : arg.entries) {
              std::visit(
                  [this, &c, &offset_delta](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;

                    auto compose_verification = [this, &c](const attribute::verification_type_info& info) -> frame_var {
                      return std::visit(
                          [this, &c](auto&& arg) -> frame_var {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, attribute::top_variable_info>) {
                              return top_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::integer_variable_info>) {
                              return int_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::float_variable_info>) {
                              return float_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::double_variable_info>) {
                              return double_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::long_variable_info>) {
                              return long_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::null_variable_info>) {
                              return null_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::uninitialized_this_variable_info>) {
                              return uninitialized_this_var();
                            }
                            if constexpr (std::is_same_v<T, attribute::object_variable_info>) {
                              return object_var(cf_.constant_pool.get_string(arg.index));
                            }
                            if constexpr (std::is_same_v<T, attribute::uninitialized_variable_info>) {
                              return uninitalized_var(get_label(arg.offset));
                            }
                          },
                          info);
                    };

                    auto off = offset_delta == 0 ? 0 : 1;
                    if constexpr (std::is_same_v<T, attribute::same_frame>) {
                      offset_delta += arg.frame_type + off;
                      auto label = get_label(offset_delta);
                      c.frames.emplace_back(label, same_frame({}, {}));
                    } else if constexpr (std::is_same_v<T, attribute::same_locals_1_stack_item_frame>) {
                      offset_delta += arg.frame_type - 64 + off;
                      auto label = get_label(offset_delta);
                      c.frames.emplace_back(label, same_frame({}, {compose_verification(arg.stack)}));
                    } else if constexpr (std::is_same_v<T, attribute::same_locals_1_stack_item_frame_extended>) {
                      offset_delta += arg.offset_delta + off;
                      auto label = get_label(offset_delta);
                      c.frames.emplace_back(label, same_frame({}, {compose_verification(arg.stack)}));
                    } else if constexpr (std::is_same_v<T, attribute::chop_frame>) {
                      offset_delta += arg.offset_delta + off;
                      auto label = get_label(offset_delta);
                      c.frames.emplace_back(label, chop_frame(251 - arg.offset_delta));
                    } else if constexpr (std::is_same_v<T, attribute::same_frame_extended>) {
                      offset_delta += arg.offset_delta + off;
                      auto label = get_label(offset_delta);
                      c.frames.emplace_back(label, same_frame({}, {}));
                    } else if constexpr (std::is_same_v<T, attribute::append_frame>) {
                      offset_delta += arg.offset_delta + off;
                      auto label = get_label(offset_delta);
                      std::vector<frame_var> locals;
                      locals.reserve(arg.locals.size());
                      for (const auto& local : arg.locals) {
                        locals.emplace_back(compose_verification(local));
                      }
                      c.frames.emplace_back(label, append_frame(locals));
                    } else if constexpr (std::is_same_v<T, attribute::full_frame>) {
                      offset_delta += arg.offset_delta + off;
                      auto label = get_label(offset_delta);
                      std::vector<frame_var> locals;
                      locals.reserve(arg.locals.size());
                      for (const auto& local : arg.locals) {
                        locals.emplace_back(compose_verification(local));
                      }
                      std::vector<frame_var> stack;
                      stack.reserve(arg.stack.size());
                      for (const auto& local : arg.stack) {
                        stack.emplace_back(compose_verification(local));
                      }
                      c.frames.emplace_back(label, full_frame(locals, stack));
                    }
                  },
                  entry);
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_visible_type_annotations>) {
            c.visible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                c.visible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          } else if constexpr (std::is_same_v<T, attribute::runtime_invisible_type_annotations>) {
            c.invisible_type_annotations.reserve(arg.annotations.size());
            for (const auto& anno : arg.annotations) {
              try {
                c.invisible_type_annotations.emplace_back(compose_type_annotation(anno));
              } catch (const std::exception& e) {
                error_handler_(e);
              }
            }
          }
        },
        attr);
  }

  std::vector<attribute::bootstrap_methods::bootstrap_method> bsms;
  for (const auto& attr : cf_.attributes) {
    if (auto* bsm = std::get_if<attribute::bootstrap_methods>(&attr)) {
      bsms = bsm->methods;
      break;
    }
  }

  const auto& bytecode = code_attr_.bytecode;

  std::vector<std::pair<size_t, instruction>> bytemap_instructions;
  auto i = 0;
  auto wide = 0;
  while (i < bytecode.size()) {
    auto insn_start = i;
    switch (const auto opcode = bytecode[i++]; opcode) {
      case op::aload_0:
      case op::aload_1:
      case op::aload_2:
      case op::aload_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::aload, opcode - op::aload_0));
        break;
      case op::astore_0:
      case op::astore_1:
      case op::astore_2:
      case op::astore_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::astore, opcode - op::astore_0));
        break;
      case op::dload_0:
      case op::dload_1:
      case op::dload_2:
      case op::dload_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::dload, opcode - op::dload_0));
        break;
      case op::dstore_0:
      case op::dstore_1:
      case op::dstore_2:
      case op::dstore_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::dstore, opcode - op::dstore_0));
        break;
      case op::fstore_0:
      case op::fstore_1:
      case op::fstore_2:
      case op::fstore_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::fstore, opcode - op::fstore_0));
        break;
      case op::fload_0:
      case op::fload_1:
      case op::fload_2:
      case op::fload_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::fload, opcode - op::fload_0));
        break;
      case op::iload_0:
      case op::iload_1:
      case op::iload_2:
      case op::iload_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::iload, opcode - op::iload_0));
        break;
      case op::istore_0:
      case op::istore_1:
      case op::istore_2:
      case op::istore_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::istore, opcode - op::istore_0));
        break;
      case op::lload_0:
      case op::lload_1:
      case op::lload_2:
      case op::lload_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::lload, opcode - op::lload_0));
        break;
      case op::lstore_0:
      case op::lstore_1:
      case op::lstore_2:
      case op::lstore_3:
        bytemap_instructions.emplace_back(insn_start, var_insn(op::lstore, opcode - op::lstore_0));
        break;
      case op::iconst_m1:
      case op::iconst_0:
      case op::iconst_1:
      case op::iconst_2:
      case op::iconst_3:
      case op::iconst_4:
      case op::iconst_5:
        bytemap_instructions.emplace_back(insn_start, push_insn(opcode - op::iconst_0));
        break;
      case op::aaload:
      case op::aastore:
      case op::aconst_null:
      case op::areturn:
      case op::arraylength:
      case op::athrow:
      case op::baload:
      case op::bastore:
      case op::caload:
      case op::castore:
      case op::d2f:
      case op::d2i:
      case op::d2l:
      case op::dadd:
      case op::daload:
      case op::dastore:
      case op::dcmpg:
      case op::dcmpl:
      case op::dconst_0:
      case op::dconst_1:
      case op::ddiv:
      case op::dmul:
      case op::dneg:
      case op::drem:
      case op::dreturn:
      case op::dsub:
      case op::dup:
      case op::dup_x1:
      case op::dup_x2:
      case op::dup2:
      case op::dup2_x1:
      case op::dup2_x2:
      case op::f2d:
      case op::f2i:
      case op::f2l:
      case op::fadd:
      case op::faload:
      case op::fastore:
      case op::fcmpg:
      case op::fcmpl:
      case op::fconst_0:
      case op::fconst_1:
      case op::fconst_2:
      case op::fdiv:
      case op::fmul:
      case op::fneg:
      case op::frem:
      case op::freturn:
      case op::fsub:
      case op::i2b:
      case op::i2c:
      case op::i2d:
      case op::i2f:
      case op::i2l:
      case op::i2s:
      case op::iadd:
      case op::iaload:
      case op::iand:
      case op::iastore:
      case op::idiv:
      case op::imul:
      case op::ineg:
      case op::ior:
      case op::irem:
      case op::ireturn:
      case op::ishl:
      case op::ishr:
      case op::isub:
      case op::iushr:
      case op::ixor:
      case op::l2d:
      case op::l2f:
      case op::l2i:
      case op::ladd:
      case op::laload:
      case op::land:
      case op::lastore:
      case op::lcmp:
      case op::lconst_0:
      case op::lconst_1:
      case op::ldiv:
      case op::lmul:
      case op::lneg:
      case op::lor:
      case op::lrem:
      case op::lreturn:
      case op::lshl:
      case op::lshr:
      case op::lsub:
      case op::lushr:
      case op::lxor:
      case op::monitorenter:
      case op::monitorexit:
      case op::nop:
      case op::pop:
      case op::pop2:
      case op::return_:
      case op::saload:
      case op::sastore:
      case op::swap:
        bytemap_instructions.emplace_back(insn_start, insn(opcode));
        break;
      case op::aload:
      case op::astore:
      case op::dload:
      case op::dstore:
      case op::fload:
      case op::fstore:
      case op::iload:
      case op::istore:
      case op::lload:
      case op::lstore:
      case op::ret: {
        auto index = static_cast<uint16_t>(bytecode[i++]);
        if (wide != 0) {
          index = (index << 8) | bytecode[i++];
        }
        bytemap_instructions.emplace_back(insn_start, var_insn(opcode, index));
        break;
      }
      case op::anewarray: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        bytemap_instructions.emplace_back(insn_start, array_insn(cf_.constant_pool.get_string(index)));
        break;
      }
      case op::bipush: {
        auto value = static_cast<int8_t>(bytecode[i++]);
        bytemap_instructions.emplace_back(insn_start, push_insn(value));
        break;
      }
      case op::checkcast:
      case op:: instanceof:
      case op::new_: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        bytemap_instructions.emplace_back(insn_start, type_insn(opcode, cf_.constant_pool.get_string(index)));
        break;
      }
      case op::getfield:
      case op::getstatic:
      case op::invokespecial:
      case op::invokestatic:
      case op::invokevirtual:
      case op::putfield:
      case op::putstatic: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto ref = ref::from(cf_, index);
        bytemap_instructions.emplace_back(insn_start, ref_insn(opcode, ref.owner, ref.name, ref.descriptor));
        break;
      }
      case op::goto_:
      case op::if_acmpeq:
      case op::if_acmpne:
      case op::if_icmpeq:
      case op::if_icmpne:
      case op::if_icmplt:
      case op::if_icmpge:
      case op::if_icmpgt:
      case op::if_icmple:
      case op::ifeq:
      case op::ifne:
      case op::iflt:
      case op::ifge:
      case op::ifgt:
      case op::ifle:
      case op::ifnonnull:
      case op::ifnull:
      case op::jsr: {
        auto offset = static_cast<int16_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto target = get_label(insn_start + offset);
        bytemap_instructions.emplace_back(insn_start, branch_insn(opcode, target));
        break;
      }
      case op::goto_w:
      case op::jsr_w: {
        auto offset = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                      static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto target = get_label(insn_start + offset);
        bytemap_instructions.emplace_back(insn_start, branch_insn(opcode, target));
        break;
      }
      case op::iinc: {
        auto index = static_cast<uint16_t>(bytecode[i++]);
        if (wide != 0) {
          index = (index << 8) | bytecode[i++];
        }
        auto value = static_cast<int16_t>(bytecode[i++]);
        if (wide != 0) {
          value = static_cast<int16_t>((value << 8) | bytecode[i++]);
        }
        bytemap_instructions.emplace_back(insn_start, iinc_insn(index, value));
      }
      case op::invokedynamic: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto& id_info = std::get<cp::invoke_dynamic_info>(cf_.constant_pool[index]);
        auto& bsm = bsms[id_info.bootstrap_method_attr_index];
        auto& nat = std::get<cp::name_and_type_info>(cf_.constant_pool[id_info.name_and_type_index]);
        auto& mh = std::get<cp::method_handle_info>(cf_.constant_pool[bsm.index]);
        auto ref = ref::from(cf_, mh.reference_index);
        std::vector<value> args;
        args.reserve(bsm.arguments.size());
        for (const auto& arg : bsm.arguments) {
          args.emplace_back(get_value(cf_, arg));
        }
        bytemap_instructions.emplace_back(
            insn_start,
            invoke_dynamic_insn(cf_.constant_pool.get_string(nat.name_index),
                                cf_.constant_pool.get_string(nat.descriptor_index),
                                method_handle(mh.reference_kind, ref.owner, ref.name, ref.descriptor), args));
        i += 2;
        break;
      }
      case op::invokeinterface: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto ref = ref::from(cf_, index);
        bytemap_instructions.emplace_back(insn_start, ref_insn(opcode, ref.owner, ref.name, ref.descriptor));
        i += 2;
        break;
      }
      case op::ldc: {
        auto index = bytecode[i++];
        bytemap_instructions.emplace_back(insn_start, push_insn(get_value(cf_, index)));
        break;
      }
      case op::ldc_w:
      case op::ldc2_w: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        bytemap_instructions.emplace_back(insn_start, push_insn(get_value(cf_, index)));
        break;
      }
      case op::lookupswitch: {
        i = (i + 3) & ~3;
        auto default_offset = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                              static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto npairs = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                      static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        std::vector<std::pair<int32_t, label>> pairs;
        pairs.reserve(npairs);
        for (auto j = 0; j < npairs; j++) {
          auto match = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                       static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
          auto offset = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                        static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
          pairs.emplace_back(match, get_label(insn_start + offset));
        }
        bytemap_instructions.emplace_back(insn_start,
                                          lookup_switch_insn(get_label(insn_start + default_offset), pairs));
        break;
      }
      case op::multianewarray: {
        auto index = static_cast<uint16_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto dimensions = bytecode[i++];
        bytemap_instructions.emplace_back(insn_start,
                                          multi_array_insn(cf_.constant_pool.get_string(index), dimensions));
        break;
      }
      case op::newarray:
        bytemap_instructions.emplace_back(insn_start, array_insn(bytecode[i++]));
        break;
      case op::sipush: {
        auto value = static_cast<int16_t>(bytecode[i++]) << 8 | bytecode[i++];
        bytemap_instructions.emplace_back(insn_start, push_insn(value));
        break;
      }
      case op::tableswitch: {
        i = (i + 3) & ~3;
        auto default_offset = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                              static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto low = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                   static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto high = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                    static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
        auto offsets = high - low + 1;
        std::vector<label> labels;
        labels.reserve(offsets);
        for (auto j = 0; j < offsets; j++) {
          auto offset = static_cast<int32_t>(bytecode[i++]) << 24 | static_cast<int32_t>(bytecode[i++]) << 16 |
                        static_cast<int32_t>(bytecode[i++]) << 8 | bytecode[i++];
          labels.push_back(get_label(insn_start + offset));
        }
        bytemap_instructions.emplace_back(insn_start,
                                          table_switch_insn(get_label(insn_start + default_offset), low, high, labels));
        break;
      }
      case op::wide:
        wide = 2;
        break;
      default:
        throw std::runtime_error("Unknown opcode (" + std::to_string(opcode) + ")");
    }
    if (wide != 0) {
      wide--;
    }
  }

  size_t last_loc = 0;
  for (const auto& [loc, in] : bytemap_instructions) {
    for (const auto& [label_loc, label] : needed_labels_) {
      if (label_loc == loc || (last_loc > label_loc && loc < label_loc)) {
        c.push_back(label);
      }
    }
    last_loc = loc;
    c.push_back(in);
  }


  return c;
}

label code_composer::get_label(uint32_t pc) {
  for (const auto& [pc_, label] : needed_labels_) {
    if (pc_ == pc) {
      return label;
    }
  }
  label l;
  needed_labels_.emplace_back(pc, l);
  return l;
}

} // namespace cafe
