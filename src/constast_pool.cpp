
#include "cafe/constant_pool.hpp"

#include <stdexcept>

#include "cafe/constants.hpp"

namespace cafe::cp {

std::string constant_pool::get_string(uint16_t index) const {
  if (index == 0 || index >= size()) {
    return "";
  }
  if (const auto utf8 = std::get_if<utf8_info>(&at(index))) {
    return utf8->value;
  }
  if (const auto string = std::get_if<string_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(string->string_index))) {
      return utf8->value;
    }
  }
  if (const auto class_info = std::get_if<cp::class_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(class_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto module_info = std::get_if<cp::module_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(module_info->name_index))) {
      return utf8->value;
    }
  }
  if (const auto package_info = std::get_if<cp::package_info>(&at(index))) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(package_info->name_index))) {
      return utf8->value;
    }
  }
  return "";
}

uint16_t constant_pool::get_utf(const std::string_view& value) {
  for (auto i = 1; i < size(); i++) {
    if (const auto utf8 = std::get_if<utf8_info>(&at(i))) {
      if (utf8->value == value) {
        return static_cast<uint16_t>(i);
      }
    }
  }
  const auto index = static_cast<uint16_t>(size());
  emplace_back(utf8_info{std::string(value)});
  return index;
}

uint16_t constant_pool::get_class(const std::string_view& value) {
  for (auto i = 1; i < size(); i++) {
    if (const auto cls = std::get_if<class_info>(&at(i))) {
      if (const auto utf8 = std::get_if<utf8_info>(&at(cls->name_index))) {
        if (utf8->value == value) {
          return static_cast<uint16_t>(i);
        }
      }
    }
  }
  auto value_index = get_utf(value);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(class_info{value_index});
  return index;
}


uint16_t constant_pool::get_string(const std::string_view& value) {
  for (auto i = 1; i < size(); i++) {
    if (const auto str = std::get_if<string_info>(&at(i))) {
      if (const auto utf8 = std::get_if<utf8_info>(&at(str->string_index))) {
        if (utf8->value == value) {
          return static_cast<uint16_t>(i);
        }
      }
    }
  }
  auto value_index = get_utf(value);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(string_info{value_index});
  return index;
}

uint16_t constant_pool::get_module(const std::string_view& value) {
  for (auto i = 1; i < size(); i++) {
    if (const auto module = std::get_if<module_info>(&at(i))) {
      if (const auto utf8 = std::get_if<utf8_info>(&at(module->name_index))) {
        if (utf8->value == value) {
          return static_cast<uint16_t>(i);
        }
      }
    }
  }
  auto value_index = get_utf(value);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(module_info{value_index});
  return index;
}

uint16_t constant_pool::get_package(const std::string_view& value) {
  for (auto i = 1; i < size(); i++) {
    if (const auto package = std::get_if<package_info>(&at(i))) {
      if (const auto utf8 = std::get_if<utf8_info>(&at(package->name_index))) {
        if (utf8->value == value) {
          return static_cast<uint16_t>(i);
        }
      }
    }
  }
  auto value_index = get_utf(value);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(package_info{value_index});
  return index;
}

uint16_t constant_pool::get_value(bsm_buffer& bsm_buffer, const value& value) {
  return std::visit(
      [this, &bsm_buffer](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, class_value>) {
          return get_class(arg.get());
        } else if constexpr (std::is_same_v<T, std::string>) {
          return get_string(arg);
        } else if constexpr (std::is_same_v<T, int32_t>) {
          for (auto i = 1; i < size(); i++) {
            if (const auto integer = std::get_if<integer_info>(&at(i))) {
              if (integer->value == arg) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto index = static_cast<uint16_t>(size());
          emplace_back(integer_info{arg});
          return index;
        } else if constexpr (std::is_same_v<T, float>) {
          for (auto i = 1; i < size(); i++) {
            if (const auto flt = std::get_if<float_info>(&at(i))) {
              if (flt->value == arg) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto index = static_cast<uint16_t>(size());
          emplace_back(float_info{arg});
          return index;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          for (auto i = 1; i < size(); i++) {
            if (const auto lng = std::get_if<long_info>(&at(i))) {
              if (lng->value == arg) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto index = static_cast<uint16_t>(size());
          emplace_back(long_info{arg});
          emplace_back(pad_info{});
          return index;
        } else if constexpr (std::is_same_v<T, double>) {
          for (auto i = 1; i < size(); i++) {
            if (const auto dbl = std::get_if<double_info>(&at(i))) {
              if (dbl->value == arg) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto index = static_cast<uint16_t>(size());
          emplace_back(double_info{arg});
          emplace_back(pad_info{});
          return index;
        } else if constexpr (std::is_same_v<T, method_handle>) {
          uint16_t index;
          switch (arg.kind) {
            case reference_kind::get_field:
            case reference_kind::get_static:
            case reference_kind::put_field:
            case reference_kind::put_static:
              index = get_field_ref(arg.owner, arg.name, arg.descriptor);
              break;
            case reference_kind::invoke_virtual:
            case reference_kind::invoke_static:
            case reference_kind::invoke_special:
            case reference_kind::new_invoke_special:
              index = get_method_ref(arg.owner, arg.name, arg.descriptor);
              break;
            case reference_kind::invoke_interface:
              index = get_interface_method_ref(arg.owner, arg.name, arg.descriptor);
              break;
            default:
              throw std::runtime_error("Invalid reference kind");
          }
          for (auto i = 1; i < size(); i++) {
            if (const auto handle = std::get_if<method_handle_info>(&at(i))) {
              if (handle->reference_kind == arg.kind && handle->reference_index == index) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto handle_index = static_cast<uint16_t>(size());
          emplace_back(method_handle_info{arg.kind, index});
          return handle_index;
        } else if constexpr (std::is_same_v<T, method_type>) {
          auto index = get_utf(arg.descriptor);
          for (auto i = 1; i < size(); i++) {
            if (const auto type = std::get_if<method_type_info>(&at(i))) {
              if (type->descriptor_index == index) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto type_index = static_cast<uint16_t>(size());
          emplace_back(method_type_info{index});
          return type_index;
        } else if constexpr (std::is_same_v<T, dynamic>) {
          auto bsm_index = bsm_buffer.get_bsm_index(*this, arg.handle, arg.args);
          auto name_and_type_index = get_name_and_type(arg.name, arg.descriptor);
          for (auto i = 1; i < size(); i++) {
            if (const auto dyn = std::get_if<dynamic_info>(&at(i))) {
              if (dyn->bootstrap_method_attr_index == bsm_index && dyn->name_and_type_index == name_and_type_index) {
                return static_cast<uint16_t>(i);
              }
            }
          }
          auto dyn_index = static_cast<uint16_t>(size());
          emplace_back(dynamic_info{bsm_index, name_and_type_index});
          return dyn_index;
        }
      },
      value);
}

uint16_t constant_pool::get_name_and_type(const std::string_view& name, const std::string_view& descriptor) {
  for (auto i = 1; i < size(); i++) {
    if (const auto name_and_type = std::get_if<name_and_type_info>(&at(i))) {
      if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->name_index))) {
        if (utf8->value == name) {
          if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->descriptor_index))) {
            if (utf8->value == descriptor) {
              return static_cast<uint16_t>(i);
            }
          }
        }
      }
    }
  }
  auto name_index = get_utf(name);
  auto desc = get_utf(descriptor);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(name_and_type_info{name_index, desc});
  return index;
}

uint16_t constant_pool::get_field_ref(const std::string_view& class_name, const std::string_view& name,
                                      const std::string_view& descriptor) {
  for (auto i = 1; i < size(); i++) {
    if (const auto field_ref = std::get_if<field_ref_info>(&at(i))) {
      if (const auto cls = std::get_if<class_info>(&at(field_ref->class_index))) {
        if (const auto utf8 = std::get_if<utf8_info>(&at(cls->name_index))) {
          if (utf8->value == class_name) {
            if (const auto name_and_type = std::get_if<name_and_type_info>(&at(field_ref->name_and_type_index))) {
              if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->name_index))) {
                if (utf8->value == name) {
                  if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->descriptor_index))) {
                    if (utf8->value == descriptor) {
                      return static_cast<uint16_t>(i);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  auto class_index = get_class(class_name);
  auto desc_index = get_name_and_type(name, descriptor);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(field_ref_info{class_index, desc_index});
  return index;
}

uint16_t constant_pool::get_method_ref(const std::string_view& class_name, const std::string_view& name,
                                       const std::string_view& descriptor) {
  for (auto i = 1; i < size(); i++) {
    if (const auto method_ref = std::get_if<method_ref_info>(&at(i))) {
      if (const auto cls = std::get_if<class_info>(&at(method_ref->class_index))) {
        if (const auto utf8 = std::get_if<utf8_info>(&at(cls->name_index))) {
          if (utf8->value == class_name) {
            if (const auto name_and_type = std::get_if<name_and_type_info>(&at(method_ref->name_and_type_index))) {
              if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->name_index))) {
                if (utf8->value == name) {
                  if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->descriptor_index))) {
                    if (utf8->value == descriptor) {
                      return static_cast<uint16_t>(i);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  auto class_index = get_class(class_name);
  auto desc_index = get_name_and_type(name, descriptor);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(method_ref_info{class_index, desc_index});
  return index;
}

uint16_t constant_pool::get_interface_method_ref(const std::string_view& class_name, const std::string_view& name,
                                                 const std::string_view& descriptor) {
  for (auto i = 1; i < size(); i++) {
    if (const auto method_ref = std::get_if<interface_method_ref_info>(&at(i))) {
      if (const auto cls = std::get_if<class_info>(&at(method_ref->class_index))) {
        if (const auto utf8 = std::get_if<utf8_info>(&at(cls->name_index))) {
          if (utf8->value == class_name) {
            if (const auto name_and_type = std::get_if<name_and_type_info>(&at(method_ref->name_and_type_index))) {
              if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->name_index))) {
                if (utf8->value == name) {
                  if (const auto utf8 = std::get_if<utf8_info>(&at(name_and_type->descriptor_index))) {
                    if (utf8->value == descriptor) {
                      return static_cast<uint16_t>(i);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  auto class_index = get_class(class_name);
  auto desc_index = get_name_and_type(name, descriptor);
  const auto index = static_cast<uint16_t>(size());
  emplace_back(interface_method_ref_info{class_index, desc_index});
  return index;
}

uint16_t constant_pool::count() const {
  uint16_t s = 0;
  for (const auto& info : *this) {
    if (std::holds_alternative<pad_info>(info)) {
      continue;
    }
    s++;
    if (std::holds_alternative<long_info>(info) || std::holds_alternative<double_info>(info)) {
      s++;
    }
  }
  return s;
}


uint16_t bsm_buffer::get_bsm_index(constant_pool& pool, const method_handle& handle, const std::vector<value>& args) {
  auto handle_index = pool.get_value(*this, handle);
  std::vector<uint16_t> args_index;
  args_index.reserve(args.size());
  for (const auto& arg : args) {
    args_index.emplace_back(pool.get_value(*this, arg));
  }
  for (auto i = 0; i < bsms.size(); i++) {
    auto& bsm = bsms[i];
    if (bsm.index == handle_index && bsm.arguments == args_index) {
      return static_cast<uint16_t>(i);
    }
  }
  const auto index = static_cast<uint16_t>(bsms.size());
  bsms.emplace_back(attribute::bootstrap_methods::bootstrap_method{handle_index, args_index});
  return index;
}


} // namespace cafe::cp
