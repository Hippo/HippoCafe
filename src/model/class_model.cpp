#include "cafe/model/class_model.hpp"

#include <sstream>

#include "cafe/class_file.hpp"
#include "cafe/data_rw.hpp"
#include "composer.hpp"
#include "decomposer.hpp"

namespace cafe {
field_model::field_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
}
std::string field_model::to_string() const {
  std::ostringstream oss;

  oss << name << ' ' << descriptor << ":\n";
  oss << "  access: 0x" << std::hex << access_flags << std::dec;
  if (synthetic || deprecated) {
    oss << ", synthetic: " << std::boolalpha << synthetic << ", deprecated: " << deprecated << std::noboolalpha;
  }
  oss << '\n';
  if (constant_value) {
    oss << "  constant_value: " << cafe::to_string(*constant_value) << '\n';
  }
  if (!signature.empty()) {
    oss << "  signature: " << signature << '\n';
  }
  if (!visible_annotations.empty()) {
    oss << "  visible_annotations:\n";
    for (const auto& anno : visible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_annotations.empty()) {
    oss << "  invisible_annotations:\n";
    for (const auto& anno : invisible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!visible_type_annotations.empty()) {
    oss << "  visible_type_annotations:\n";
    for (const auto& anno : visible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_type_annotations.empty()) {
    oss << "  invisible_type_annotations:\n";
    for (const auto& anno : invisible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  auto str = oss.str();
  while (str.back() == '\n') {
    str.pop_back();
  }
  return str;
}
method_model::method_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
}
std::string method_model::to_string() const {
  std::ostringstream oss;

  oss << name << descriptor << ":\n";
  for (const auto& in : code) {
    oss << "  " << cafe::to_string(in) << "\n";
  }
  oss << "access: 0x" << std::hex << access_flags << std::dec << ", max_stack: " << code.max_stack
      << ", max_locals: " << code.max_locals << "\n";
  if (!code.visible_type_annotations.empty()) {
    oss << "  code_visible_type_annotations:\n";
    for (const auto& anno : code.visible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!code.invisible_type_annotations.empty()) {
    oss << "  code_invisible_type_annotations:\n";
    for (const auto& anno : code.invisible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!code.tcb_table.empty()) {
    oss << "  try_catch_blocks:\n";
    for (const auto& tcb : code.tcb_table) {
      oss << "    " << tcb.to_string() << "\n";
    }
  }
  if (!code.line_numbers.empty()) {
    oss << "  line_numbers:\n";
    for (const auto& [label, num] : code.line_numbers) {
      oss << "  " << label.to_string() << " -> " << num << "\n";
    }
  }
  if (!code.local_vars.empty()) {
    oss << "  local_variables:\n";
    for (const auto& lv : code.local_vars) {
      oss << "    " << lv.to_string() << "\n";
    }
  }
  if (!code.frames.empty()) {
    oss << "  stack_map_frames:\n";
    for (const auto& [label, frame] : code.frames) {
      oss << "    " << label.to_string() << " -> " << cafe::to_string(frame) << "\n";
    }
  }
  if (synthetic || deprecated) {
    oss << "  synthetic: " << std::boolalpha << synthetic << ", deprecated: " << deprecated << std::noboolalpha << "\n";
  }
  if (!signature.empty()) {
    oss << "  signature: " << signature << "\n";
  }
  if (!visible_annotations.empty()) {
    oss << "  visible_annotations:\n";
    for (const auto& anno : visible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_annotations.empty()) {
    oss << "  invisible_annotations:\n";
    for (const auto& anno : invisible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!visible_type_annotations.empty()) {
    oss << "  visible_type_annotations:\n";
    for (const auto& anno : visible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_type_annotations.empty()) {
    oss << "  invisible_type_annotations:\n";
    for (const auto& anno : invisible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!visible_parameter_annotations.empty()) {
    oss << "  visible_parameter_annotations:\n";
    auto param = 0;
    for (const auto& anno : visible_parameter_annotations) {
      oss << "    " << param << ":\n";
      for (const auto& a : anno) {
        oss << "      " << a.to_string() << "\n";
      }
      ++param;
    }
  }
  if (!invisible_parameter_annotations.empty()) {
    oss << "  invisible_parameter_annotations:\n";
    auto param = 0;
    for (const auto& anno : invisible_parameter_annotations) {
      oss << "    " << param << ":\n";
      for (const auto& a : anno) {
        oss << "      " << a.to_string() << "\n";
      }
      ++param;
    }
  }
  if (annotation_default) {
    oss << "  annotation_default: " << annotation_default->to_string() << "\n";
  }
  if (!method_parameters.empty()) {
    oss << "method_parameters:\n";
    for (const auto& [access, name] : method_parameters) {
      oss << "    " << access << " " << name << "\n";
    }
  }

  if (!exceptions.empty()) {
    oss << "  exceptions:\n";
    for (const auto& ex : exceptions) {
      oss << "    " << ex << "\n";
    }
  }

  auto str = oss.str();
  while (std::isspace(str.back())) {
    str.pop_back();
  }
  return str;
}
inner_class::inner_class(const std::string_view& name, const std::string_view& outer_name,
                         const std::string_view& inner_name, uint16_t access_flags) :
    name(name), outer_name(outer_name), inner_name(inner_name), access_flags(access_flags) {
}
requires_model::requires_model(const std::string_view& name, uint16_t access_flags, const std::string_view& version) :
    name(name), access_flags(access_flags), version(version) {
}
exports_model::exports_model(const std::string_view& package, uint16_t access_flags,
                             const std::vector<std::string>& modules) :
    package(package), access_flags(access_flags), modules(modules) {
}
opens_model::opens_model(const std::string_view& package, uint16_t access_flags,
                         const std::vector<std::string>& modules) :
    package(package), access_flags(access_flags), modules(modules) {
}
provides_model::provides_model(const std::string_view& service, const std::vector<std::string>& providers) :
    service(service), providers(providers) {
}
module_model::module_model(const std::string_view& name, uint16_t access_flags, const std::string_view& version) :
    name(name), access_flags(access_flags), version(version) {
}
record_component::record_component(const std::string_view& name, const std::string_view& descriptor) :
    name(name), descriptor(descriptor) {
}
class_model::class_model(uint32_t version, uint16_t access_flags, const std::string_view& name,
                         const std::string_view& super_name) :
    version(version), access_flags(access_flags), name(name), super_name(super_name) {
}

std::string class_model::to_string() const {
  std::ostringstream oss;

  oss << "class " << name;
  if (!super_name.empty()) {
    oss << " extends " << super_name;
  }
  if (!interfaces.empty()) {
    oss << " implements ";
    bool first = true;
    for (const auto& iface : interfaces) {
      if (!first) {
        oss << ", ";
      }
      oss << iface;
      first = false;
    }
  }
  oss << ":\n";
  oss << "  version: " << (version >> 16) << '.' << (version & 0xFFFF) << ", access: 0x" << std::hex << access_flags
      << std::dec << "\n";
  for (const auto& field : fields) {
    oss << field.to_string() << "\n";
  }
  for (const auto& method : methods) {
    oss << method.to_string() << "\n";
  }
  if (synthetic || deprecated) {
    oss << "  synthetic: " << std::boolalpha << synthetic << ", deprecated: " << deprecated << std::noboolalpha << "\n";
  }
  if (!signature.empty()) {
    oss << "  signature: " << signature << "\n";
  }
  if (!source_file.empty()) {
    oss << "  source_file: " << source_file << "\n";
  }
  if (!inner_classes.empty()) {
    oss << "  inner_classes:\n";
    for (const auto& inner : inner_classes) {
      oss << "    " << inner.name << " " << inner.outer_name << " " << inner.inner_name << " 0x" << std::hex
          << inner.access_flags << std::dec << "\n";
    }
  }
  if (!enclosing_owner.empty() && !enclosing_name.empty() && !enclosing_descriptor.empty()) {
    oss << "  enclosing_method: " << enclosing_owner << "#" << enclosing_name << enclosing_descriptor << "\n";
  }
  if (!source_debug_extension.empty()) {
    oss << "  source_debug_extension: " << source_debug_extension << "\n";
  }
  if (module) {
    oss << "  module " << module->name << ":\n";
    oss << "    access: 0x" << std::hex << module->access_flags << std::dec << ", version: " << module->version << "\n";
    oss << "    requires:\n";
    for (const auto& req : module->require_models) {
      oss << "      " << req.name << " 0x" << std::hex << req.access_flags << std::dec << " " << req.version << "\n";
    }
    oss << "    exports:\n";
    for (const auto& exp : module->exports) {
      oss << "      " << exp.package << " 0x" << std::hex << exp.access_flags << std::dec << " [";
      bool first = true;
      for (const auto& mod : exp.modules) {
        if (!first) {
          oss << ", ";
        }
        oss << mod;
        first = false;
      }
      oss << "]\n";
    }
    oss << "    opens:\n";
    for (const auto& op : module->opens) {
      oss << "      " << op.package << " 0x" << std::hex << op.access_flags << std::dec << " [";
      bool first = true;
      for (const auto& mod : op.modules) {
        if (!first) {
          oss << ", ";
        }
        oss << mod;
        first = false;
      }
      oss << "]\n";
    }
    oss << "    uses:\n";
    for (const auto& use : module->uses) {
      oss << "      " << use << "\n";
    }
    oss << "    provides:\n";
    for (const auto& prov : module->provides) {
      oss << "      " << prov.service << " [";
      bool first = true;
      for (const auto& p : prov.providers) {
        if (!first) {
          oss << ", ";
        }
        oss << p;
        first = false;
      }
      oss << "]\n";
    }
  }
  if (!module_packages.empty()) {
    oss << "  module_packages:\n";
    for (const auto& pkg : module_packages) {
      oss << "    " << pkg << "\n";
    }
  }
  if (!module_main_class.empty()) {
    oss << "  module_main_class: " << module_main_class << "\n";
  }
  if (!nest_host.empty()) {
    oss << "  nest_host: " << nest_host << "\n";
  }
  if (!nest_members.empty()) {
    oss << "  nest_members:\n";
    for (const auto& member : nest_members) {
      oss << "    " << member << "\n";
    }
  }

  if (!record_components.empty()) {
    oss << "  record_components:\n";
    for (const auto& comp : record_components) {
      oss << "    " << comp.name << " " << comp.descriptor << ":\n";
      if (!comp.signature.empty()) {
        oss << "      signature: " << comp.signature << "\n";
      }
      if (!comp.visible_annotations.empty()) {
        oss << "      visible_annotations:\n";
        for (const auto& anno : comp.visible_annotations) {
          oss << "        " << anno.to_string() << "\n";
        }
      }
      if (!comp.invisible_annotations.empty()) {
        oss << "      invisible_annotations:\n";
        for (const auto& anno : comp.invisible_annotations) {
          oss << "        " << anno.to_string() << "\n";
        }
      }
      if (!comp.visible_type_annotations.empty()) {
        oss << "      visible_type_annotations:\n";
        for (const auto& anno : comp.visible_type_annotations) {
          oss << "        " << anno.to_string() << "\n";
        }
      }
      if (!comp.invisible_type_annotations.empty()) {
        oss << "      invisible_type_annotations:\n";
        for (const auto& anno : comp.invisible_type_annotations) {
          oss << "        " << anno.to_string() << "\n";
        }
      }
    }
  }

  if (!visible_annotations.empty()) {
    oss << "  visible_annotations:\n";
    for (const auto& anno : visible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_annotations.empty()) {
    oss << "  invisible_annotations:\n";
    for (const auto& anno : invisible_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!visible_type_annotations.empty()) {
    oss << "  visible_type_annotations:\n";
    for (const auto& anno : visible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  if (!invisible_type_annotations.empty()) {
    oss << "  invisible_type_annotations:\n";
    for (const auto& anno : invisible_type_annotations) {
      oss << "    " << anno.to_string() << "\n";
    }
  }
  auto str = oss.str();
  while (std::isspace(str.back())) {
    str.pop_back();
  }
  return str;
}
std::istream& operator>>(std::istream& stream, class_model& model) {
  class_file cf;
  stream >> cf;
  class_composer composer(cf, [](const std::exception&) {});
  composer.compose(model);
  return stream;
}
std::ostream& operator<<(std::ostream& stream, const class_model& model) {
  data_writer writer(stream);
  decomposer_ctx ctx(writer);
  ctx.pool.emplace_back(cp::pad_info{});
  class_decomposer decomposer(ctx);
  const auto cf = decomposer.decompose(model);
  return stream << cf;
}
} // namespace cafe
