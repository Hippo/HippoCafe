#include "cafe/class_writer.hpp"

#include <iostream>

#include "cafe/constants.hpp"

#include "../include/cafe/data_writer.hpp"
#include "cafe/analysis.hpp"
#include "visitor.hpp"


namespace cafe {
class_writer::class_writer(uint8_t flags) : flags_(flags) {
}
class_writer::class_writer(const class_tree& tree, uint8_t flags) : flags_(flags), tree_(&tree) {
}
void class_writer::write_source_file(const std::string_view& source_file) {
  const auto attr_name = get_utf("SourceFile");
  const auto source_index = get_utf(source_file);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(source_index);
  attributes_count_++;
}
void class_writer::write_enclosing_method(const std::string_view& owner,
                                          const std::optional<std::pair<std::string, std::string>>& method) {
  const auto attr_name = get_utf("EnclosingMethod");
  const auto class_index = get_class(owner);
  auto nat_index = 0;
  if (method) {
    const auto [name, desc] = *method;
    nat_index = get_name_and_type(name, desc);
  }
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(4);
  buf.write_u16(class_index);
  buf.write_u16(nat_index);
  attributes_count_++;
}
void class_writer::write_source_debug_extension(const std::string_view& debug_extension) {
  const auto attr_name = get_utf("SourceDebugExtension");
  const auto& data = std::vector<int8_t>(debug_extension.begin(), debug_extension.end());
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size()));
  buf.write_all(data);
  attributes_count_++;
}
void class_writer::write_signature(const std::string_view& signature) {
  const auto attr_name = get_utf("Signature");
  const auto sig_index = get_utf(signature);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(sig_index);
  attributes_count_++;
}
void class_writer::write_inner_classes(const std::vector<inner_class>& inner_classes) {
  const auto attr_name = get_utf("InnerClasses");
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>((inner_classes.size() * 8) + 2));
  buf.write_u16(static_cast<uint16_t>(inner_classes.size()));
  for (const auto& [name, outer, inner, access] : inner_classes) {
    buf.write_u16(get_class(name));
    buf.write_u16(outer ? get_class(*outer) : 0);
    buf.write_u16(inner ? get_utf(*inner) : 0);
    buf.write_u16(access);
  }
  attributes_count_++;
}
void class_writer::write_module(const module& module) {
  data_writer buf;
  buf.write_u16(get_module(module.name));
  buf.write_u16(module.access_flags);
  buf.write_u16(module.version ? get_utf(*module.version) : 0);
  buf.write_u16(static_cast<uint16_t>(module.mod_requires.size()));
  for (const auto& req : module.mod_requires) {
    buf.write_u16(get_module(req.module));
    buf.write_u16(req.access_flags);
    buf.write_u16(req.version ? get_utf(*req.version) : 0);
  }
  buf.write_u16(static_cast<uint16_t>(module.exports.size()));
  for (const auto& exp : module.exports) {
    buf.write_u16(get_package(exp.package));
    buf.write_u16(exp.access_flags);
    buf.write_u16(static_cast<uint16_t>(exp.modules.size()));
    for (const auto& name : exp.modules) {
      buf.write_u16(get_module(name));
    }
  }
  buf.write_u16(static_cast<uint16_t>(module.opens.size()));
  for (const auto& open : module.opens) {
    buf.write_u16(get_package(open.package));
    buf.write_u16(open.access_flags);
    buf.write_u16(static_cast<uint16_t>(open.modules.size()));
    for (const auto& name : open.modules) {
      buf.write_u16(get_module(name));
    }
  }
  buf.write_u16(static_cast<uint16_t>(module.uses.size()));
  for (const auto& use : module.uses) {
    buf.write_u16(get_class(use));
  }
  buf.write_u16(static_cast<uint16_t>(module.provides.size()));
  for (const auto& prov : module.provides) {
    buf.write_u16(get_class(prov.service));
    buf.write_u16(static_cast<uint16_t>(prov.providers.size()));
    for (const auto& provider : prov.providers) {
      buf.write_u16(get_class(provider));
    }
  }
  const auto attr_name = get_utf("Module");
  const auto module_data = buf.data();
  databuf data(attributes_);
  data.write_u16(attr_name);
  data.write_u32(static_cast<uint32_t>(module_data.size()));
  data.write_all(module_data);
  attributes_count_++;
}
void class_writer::write_module_packages(const std::vector<std::string>& module_packages) {
  const auto attr_name = get_utf("ModulePackages");
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(module_packages.size() + 2));
  buf.write_u16(static_cast<uint16_t>(module_packages.size()));
  for (const auto& name : module_packages) {
    buf.write_u16(get_package(name));
  }
  attributes_count_++;
}
void class_writer::write_module_main_class(const std::string_view& main_class) {
  const auto attr_name = get_utf("ModuleMainClass");
  const auto module_class = get_class(main_class);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(module_class);
  attributes_count_++;
}
void class_writer::write_nest_host(const std::string_view& host) {
  const auto attr_name = get_utf("NestHost");
  const auto nest_host = get_class(host);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(2);
  buf.write_u16(nest_host);
  attributes_count_++;
}
void class_writer::write_nest_members(const std::vector<std::string>& members) {
  const auto attr_name = get_utf("NestMembers");
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>((members.size() * 2) + 2));
  buf.write_u16(static_cast<uint16_t>(members.size()));
  for (const auto& name : members) {
    buf.write_u16(get_class(name));
  }
  attributes_count_++;
}
void class_writer::write_record(const std::vector<record_component>& components) {
  data_writer buf;
  buf.write_u16(static_cast<uint16_t>(components.size()));
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (const auto& comp : components) {
    buf.write_u16(get_utf(comp.name));
    buf.write_u16(get_utf(comp.desc));

    data_writer attr_buf;
    uint16_t attr_count = 0;
    if (comp.signature) {
      const auto attr_name = get_utf("Signature");
      const auto sig = get_utf(*comp.signature);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(2);
      attr_buf.write_u16(sig);
      attr_count++;
    }
    if (!comp.visible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleAnnotations");
      const auto& data = get_annotations(comp.visible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(comp.visible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!comp.invisible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleAnnotations");
      const auto& data = get_annotations(comp.invisible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(comp.invisible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!comp.visible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleTypeAnnotations");
      const auto& data = get_type_annotations(comp.visible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(comp.visible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!comp.invisible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleTypeAnnotations");
      const auto& data = get_type_annotations(comp.invisible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(comp.invisible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    for (const auto& attr : comp.attributes) {
      const auto attr_name = get_utf(attr.name);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(attr.data.size()));
      attr_buf.write_all(attr.data);
      attr_count++;
    }
    buf.write_u16(attr_count);
    buf.write_all(attr_buf.data());
  }
  const auto attr_name = get_utf("Record");
  const auto& data = buf.data();
  databuf data_buf(attributes_);
  data_buf.write_u16(attr_name);
  data_buf.write_u32(static_cast<uint32_t>(data.size()));
  data_buf.write_all(data);
  attributes_count_++;
}
void class_writer::write_permitted_subclasses(const std::vector<std::string>& subclasses) {
  const auto attr_name = get_utf("PermittedSubclasses");
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>((subclasses.size() * 2) + 2));
  buf.write_u16(static_cast<uint16_t>(subclasses.size()));
  for (const auto& name : subclasses) {
    buf.write_u16(get_class(name));
  }
  attributes_count_++;
}
void class_writer::write_annotations(const std::vector<annotation>& annotations, bool visible) {
  const auto attr_name = get_utf(visible ? "RuntimeVisibleAnnotations" : "RuntimeInvisibleAnnotations");
  const auto& data = get_annotations(annotations);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size() + 2));
  buf.write_u16(static_cast<uint16_t>(annotations.size()));
  buf.write_all(data);
  attributes_count_++;
}
void class_writer::write_type_annotations(const std::vector<type_annotation>& annotations, bool visible) {
  const auto attr_name = get_utf(visible ? "RuntimeVisibleTypeAnnotations" : "RuntimeInvisibleTypeAnnotations");
  std::vector<std::pair<size_t, label>> dummy_labels;
  const auto& data = get_type_annotations(annotations, dummy_labels);
  databuf buf(attributes_);
  buf.write_u16(attr_name);
  buf.write_u32(static_cast<uint32_t>(data.size() + 2));
  buf.write_u16(static_cast<uint16_t>(annotations.size()));
  buf.write_all(data);
  attributes_count_++;
}
void class_writer::write_fields(const std::vector<field>& fields) {
  databuf buf(fields_);
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (const auto& field : fields) {
    const auto name = get_utf(field.name);
    const auto desc = get_utf(field.desc);
    buf.write_u16(field.access_flags);
    buf.write_u16(name);
    buf.write_u16(desc);

    uint16_t attr_count = 0;
    data_writer attr_buf;
    if (field.signature) {
      const auto attr_name = get_utf("Signature");
      const auto sig = get_utf(*field.signature);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(2);
      attr_buf.write_u16(sig);
      attr_count++;
    }
    if (field.synthetic) {
      attr_buf.write_u16(get_utf("Synthetic"));
      attr_buf.write_u32(0);
      attr_count++;
    }
    if (field.deprecated) {
      attr_buf.write_u16(get_utf("Deprecated"));
      attr_buf.write_u32(0);
      attr_count++;
    }
    if (!field.visible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleAnnotations");
      const auto& data = get_annotations(field.visible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(field.visible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!field.invisible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleAnnotations");
      const auto& data = get_annotations(field.invisible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(field.invisible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!field.visible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleTypeAnnotations");
      const auto& data = get_type_annotations(field.visible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(field.visible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!field.invisible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleTypeAnnotations");
      const auto& data = get_type_annotations(field.invisible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(field.invisible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    for (const auto& attr : field.attributes) {
      const auto attr_name = get_utf(attr.name);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(attr.data.size()));
      attr_buf.write_all(attr.data);
      attr_count++;
    }
    buf.write_u16(attr_count);
    buf.write_all(attr_buf.data());
  }
}
void class_writer::write_methods(const class_file&file, const std::vector<method>& methods, bool oak) {
  databuf buf(methods_);
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (const auto& method : methods) {
    const auto name = get_utf(method.name);
    const auto desc = get_utf(method.desc);
    buf.write_u16(method.access_flags);
    buf.write_u16(name);
    buf.write_u16(desc);

    uint16_t attr_count = 0;
    data_writer attr_buf;
    if (!method.exceptions.empty()) {
      const auto attr_name = get_utf("Exceptions");
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>((method.exceptions.size() * 2) + 2));
      attr_buf.write_u16(static_cast<uint16_t>(method.exceptions.size()));
      for (const auto& exc : method.exceptions) {
        attr_buf.write_u16(get_class(exc));
      }
      attr_count++;
    }
    if (!method.visible_parameter_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleParameterAnnotations");
      data_writer anno_writer;
      for (const auto& annos : method.visible_parameter_annotations) {
        const auto anno = get_annotations(annos);
        anno_writer.write_u16(static_cast<uint16_t>(annos.size()));
        anno_writer.write_all(anno);
      }
      const auto& data = anno_writer.data();
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 1));
      attr_buf.write_u8(static_cast<uint8_t>(method.visible_parameter_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.invisible_parameter_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleParameterAnnotations");
      data_writer anno_writer;
      for (const auto& annos : method.invisible_parameter_annotations) {
        const auto anno = get_annotations(annos);
        anno_writer.write_u16(static_cast<uint16_t>(annos.size()));
        anno_writer.write_all(anno);
      }
      const auto& data = anno_writer.data();
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 1));
      attr_buf.write_u8(static_cast<uint8_t>(method.invisible_parameter_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (method.annotation_default) {
      const auto attr_name = get_utf("AnnotationDefault");
      data_writer anno_writer;
      get_element_value(anno_writer, *method.annotation_default);
      const auto& data = anno_writer.data();
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.parameters.empty()) {
      const auto attr_name = get_utf("MethodParameters");
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>((method.parameters.size() * 4) + 1));
      attr_buf.write_u8(static_cast<uint8_t>(method.parameters.size()));
      for (const auto& [access, name] : method.parameters) {
        attr_buf.write_u16(name ? get_utf(*name) : 0);
        attr_buf.write_u16(access);
      }
      attr_count++;
    }
    if (method.synthetic) {
      attr_buf.write_u16(get_utf("Synthetic"));
      attr_buf.write_u32(0);
      attr_count++;
    }
    if (method.deprecated) {
      attr_buf.write_u16(get_utf("Deprecated"));
      attr_buf.write_u32(0);
      attr_count++;
    }
    if (method.signature) {
      const auto attr_name = get_utf("Signature");
      const auto sig = get_utf(*method.signature);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(2);
      attr_buf.write_u16(sig);
      attr_count++;
    }
    if (!method.visible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleAnnotations");
      const auto& data = get_annotations(method.visible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(method.visible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.invisible_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleAnnotations");
      const auto& data = get_annotations(method.invisible_annotations);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(method.invisible_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.visible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeVisibleTypeAnnotations");
      const auto& data = get_type_annotations(method.visible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(method.visible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.invisible_type_annotations.empty()) {
      const auto attr_name = get_utf("RuntimeInvisibleTypeAnnotations");
      const auto& data = get_type_annotations(method.invisible_type_annotations, dummy_labels);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
      attr_buf.write_u16(static_cast<uint16_t>(method.invisible_type_annotations.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    if (!method.code.empty() || !method.code.visible_type_annotations.empty() ||
        !method.code.invisible_type_annotations.empty() || !method.code.attributes.empty()) {
      const auto attr_name = get_utf("Code");
      const auto& data = write_code(file, method, method.code, oak);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(data.size()));
      attr_buf.write_all(data);
      attr_count++;
    }
    for (const auto& attr : method.attributes) {
      const auto attr_name = get_utf(attr.name);
      attr_buf.write_u16(attr_name);
      attr_buf.write_u32(static_cast<uint32_t>(attr.data.size()));
      attr_buf.write_all(attr.data);
      attr_count++;
    }
    buf.write_u16(attr_count);
    buf.write_all(attr_buf.data());
  }
}
std::vector<int8_t> class_writer::write_code(const class_file& file, const method& method, const code& c, bool oak) {
  auto max_stack = c.max_stack;
  auto max_locals = c.max_locals;
  std::vector<std::pair<label, frame>> frames = c.frames;
  std::vector<std::pair<code::const_iterator, label>> inject_labels;

  if ((flags_ & compute_maxes) != 0) {
    basic_block_graph graph(c);
    const auto [locals, stack] = graph.compute_maxes(basic_block_graph::get_start_locals(method));
    max_stack = stack;
    max_locals = locals;
  } else if ((flags_ & compute_frames) != 0) {
    basic_block_graph graph(c);
    if (tree_ == nullptr) {
      const auto [locals, stack] = graph.compute_maxes(basic_block_graph::get_start_locals(method));
      max_stack = stack;
      max_locals = locals;
    } else {
      const auto result = graph.compute_frames(*tree_, file.name, basic_block_graph::get_start_locals(file.name, method));
      max_stack = result.max_stack();
      max_locals = result.max_locals();
      frames = result.frames();
      inject_labels = result.labels();
    }
  }

  std::vector<uint8_t> code;
  std::vector<std::pair<size_t, label>> labels;
  std::vector<std::tuple<label, size_t, bool, size_t>> branches;
  std::vector<size_t> switch_pads;
  const auto shift = [&](size_t start, int32_t offset) {
    for (auto& pos : switch_pads) {
      if (pos > start) {
        pos += offset;
      }
    }
    for (auto& [target, pos, wide, insn_start] : branches) {
      if (pos >= start) {
        pos += offset;
      }
      if (insn_start >= start) {
        insn_start += offset;
      }
    }
    for (auto& [pos, target] : labels) {
      if (pos >= start) {
        pos += offset;
      }
    }
  };
  const auto get_label = [&labels](const label& lbl) -> size_t {
    for (const auto& [pos, label] : labels) {
      if (label == lbl) {
        return pos;
      }
    }
    return 0;
  };
  size_t last_label = 0;
  bool first_frame = true;
  const auto next_delta = [&](const label& target) -> uint16_t {
    const auto pos = get_label(target);
    auto delta = static_cast<uint16_t>(pos - last_label);
    if (first_frame) {
      first_frame = false;
    } else {
      delta--;
    }
    last_label = pos;
    return delta;
  };
  const auto write_frame_var = [&](data_writer& buf, const frame_var& var) {
    std::visit(
        [&](const auto& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, top_var>) {
            buf.write_u8(0);
          } else if constexpr (std::is_same_v<T, int_var>) {
            buf.write_u8(1);
          } else if constexpr (std::is_same_v<T, float_var>) {
            buf.write_u8(2);
          } else if constexpr (std::is_same_v<T, null_var>) {
            buf.write_u8(5);
          } else if constexpr (std::is_same_v<T, uninitialized_this_var>) {
            buf.write_u8(6);
          } else if constexpr (std::is_same_v<T, object_var>) {
            const auto cpool_index = get_class(arg.type);
            buf.write_u8(7);
            buf.write_u16(cpool_index);
          } else if constexpr (std::is_same_v<T, uninitialized_var>) {
            const auto offset_pc = get_label(arg.offset);
            buf.write_u8(8);
            buf.write_u16(static_cast<uint16_t>(offset_pc));
          } else if constexpr (std::is_same_v<T, long_var>) {
            buf.write_u8(4);
          } else if constexpr (std::is_same_v<T, double_var>) {
            buf.write_u8(3);
          }
        },
        var);
  };

  for (auto it = c.begin(); it != c.end(); ++it) {
    for (const auto& [it2, lbl] : inject_labels) {
      if (it == it2) {
        labels.emplace_back(code.size(), lbl);
      }
    }
    const auto& i = *it;
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, label>) {
            labels.emplace_back(code.size(), arg);
          }
          if constexpr (std::is_same_v<T, insn>) {
            code.emplace_back(arg.opcode);
          }
          if constexpr (std::is_same_v<T, var_insn>) {
            const auto opcode = arg.opcode;
            const auto index = arg.index;
            if (index <= 3) {
              switch (opcode) {
                case op::iload:
                  code.emplace_back(op::iload_0 + index);
                  return;
                case op::fload:
                  code.emplace_back(op::fload_0 + index);
                  return;
                case op::aload:
                  code.emplace_back(op::aload_0 + index);
                  return;
                case op::dload:
                  code.emplace_back(op::dload_0 + index);
                  return;
                case op::lload:
                  code.emplace_back(op::lload_0 + index);
                  return;
                case op::istore:
                  code.emplace_back(op::istore_0 + index);
                  return;
                case op::fstore:
                  code.emplace_back(op::fstore_0 + index);
                  return;
                case op::astore:
                  code.emplace_back(op::astore_0 + index);
                  return;
                case op::dstore:
                  code.emplace_back(op::dstore_0 + index);
                  return;
                case op::lstore:
                  code.emplace_back(op::lstore_0 + index);
                  return;
                default:
                  break;
              }
            }
            const auto wide = index > std::numeric_limits<uint8_t>::max();
            if (wide) {
              code.emplace_back(op::wide);
            }
            code.emplace_back(opcode);
            if (wide) {
              code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(index & 0xff));
            } else {
              code.emplace_back(static_cast<uint8_t>(index));
            }
          }
          if constexpr (std::is_same_v<T, type_insn>) {
            const auto index = get_class(arg.type);
            code.emplace_back(arg.opcode);
            code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(index & 0xff));
          }
          if constexpr (std::is_same_v<T, field_insn>) {
            const auto index = get_field_ref(arg.owner, arg.name, arg.desc);
            code.emplace_back(arg.opcode);
            code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(index & 0xff));
          }
          if constexpr (std::is_same_v<T, method_insn>) {
            auto index = arg.interface ? get_interface_method_ref(arg.owner, arg.name, arg.desc)
                                       : get_method_ref(arg.owner, arg.name, arg.desc);
            code.emplace_back(arg.opcode);
            code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(index & 0xff));
            if (arg.opcode == op::invokeinterface) {
              const type type(arg.desc);
              const auto parsed_arg = type.parameter_types();
              auto size = 1;
              for (const auto& t : parsed_arg) {
                size += t.size();
              }
              code.emplace_back(static_cast<uint8_t>(size));
              code.emplace_back(0);
            }
          }
          if constexpr (std::is_same_v<T, iinc_insn>) {
            const auto index = arg.index;
            const auto value = arg.value;
            const auto wide = index > std::numeric_limits<uint8_t>::max() ||
                              value > std::numeric_limits<int8_t>::max() || value < std::numeric_limits<int8_t>::min();

            if (wide) {
              code.emplace_back(op::wide);
            }
            code.emplace_back(op::iinc);
            if (wide) {
              code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(index & 0xff));
              code.emplace_back(static_cast<uint8_t>((value >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(value & 0xff));
            } else {
              code.emplace_back(static_cast<uint8_t>(index));
              code.emplace_back(static_cast<uint8_t>(value));
            }
          }
          if constexpr (std::is_same_v<T, push_insn>) {
            const auto& val = arg.value;
            const auto opcode = arg.opcode();
            if (opcode == op::bipush) {
              code.emplace_back(opcode);
              code.emplace_back(static_cast<int8_t>(std::get<int32_t>(val)));
            } else if (opcode == op::sipush) {
              const auto value = static_cast<int16_t>(std::get<int32_t>(val));
              code.emplace_back(opcode);
              code.emplace_back(static_cast<uint8_t>((value >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(value & 0xff));
            } else if (opcode != op::ldc) {
              code.emplace_back(opcode);
            } else {
              const auto index = get_value(val);
              if (std::holds_alternative<int64_t>(val) || std::holds_alternative<double>(val)) {
                code.emplace_back(op::ldc2_w);
                code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
                code.emplace_back(static_cast<uint8_t>(index & 0xff));
              } else if (index > std::numeric_limits<int8_t>::max()) {
                code.emplace_back(op::ldc_w);
                code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
                code.emplace_back(static_cast<uint8_t>(index & 0xff));
              } else {
                code.emplace_back(op::ldc);
                code.emplace_back(static_cast<int8_t>(index));
              }
            }
          }
          if constexpr (std::is_same_v<T, branch_insn>) {
            auto opcode = arg.opcode;
            const auto target = arg.target;
            const auto jmp = opcode == op::goto_ || opcode == op::jsr || opcode == op::jsr_w || opcode == op::goto_w;
            if (jmp) {
              const auto est = get_label(target);
              if (est != 0) {
                const auto offset = static_cast<int32_t>(est) - static_cast<int32_t>(code.size());
                if (offset > std::numeric_limits<int16_t>::max() || offset < std::numeric_limits<int16_t>::min()) {
                  if (opcode == op::goto_ || opcode == op::goto_w) {
                    opcode = op::goto_w;
                  } else {
                    opcode = op::jsr_w;
                  }
                }
              }
            }
            code.emplace_back(opcode);
            branches.emplace_back(target, code.size(), opcode == op::goto_w || opcode == op::jsr_w, code.size() - 1);
            code.emplace_back(0);
            code.emplace_back(0);
            if (opcode == op::goto_w || opcode == op::jsr_w) {
              code.emplace_back(0);
              code.emplace_back(0);
            }
          }
          if constexpr (std::is_same_v<T, lookup_switch_insn>) {
            const auto default_target = arg.default_target;
            const auto& targets = arg.targets;
            const auto insn_pos = code.size();
            code.emplace_back(op::lookupswitch);
            switch_pads.emplace_back(code.size());
            branches.emplace_back(default_target, code.size(), true, insn_pos);
            code.insert(code.end(), 4, 0);
            const auto target_size = targets.size();
            code.emplace_back(static_cast<uint8_t>((target_size >> 24) & 0xff));
            code.emplace_back(static_cast<uint8_t>((target_size >> 16) & 0xff));
            code.emplace_back(static_cast<uint8_t>((target_size >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(target_size & 0xff));
            for (const auto& [key, target] : targets) {
              code.emplace_back(static_cast<uint8_t>((key >> 24) & 0xff));
              code.emplace_back(static_cast<uint8_t>((key >> 16) & 0xff));
              code.emplace_back(static_cast<uint8_t>((key >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(key & 0xff));
              branches.emplace_back(target, code.size(), true, insn_pos);
              code.insert(code.end(), 4, 0);
            }
          }
          if constexpr (std::is_same_v<T, table_switch_insn>) {
            const auto default_target = arg.default_target;
            const auto low = arg.low;
            const auto high = arg.high;
            const auto& targets = arg.targets;
            const auto insn_pos = code.size();
            code.emplace_back(op::tableswitch);
            switch_pads.emplace_back(code.size());
            branches.emplace_back(default_target, code.size(), true, insn_pos);
            code.insert(code.end(), 4, 0);
            code.emplace_back(static_cast<uint8_t>((low >> 24) & 0xff));
            code.emplace_back(static_cast<uint8_t>((low >> 16) & 0xff));
            code.emplace_back(static_cast<uint8_t>((low >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(low & 0xff));
            code.emplace_back(static_cast<uint8_t>((high >> 24) & 0xff));
            code.emplace_back(static_cast<uint8_t>((high >> 16) & 0xff));
            code.emplace_back(static_cast<uint8_t>((high >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(high & 0xff));
            for (const auto& target : targets) {
              branches.emplace_back(target, code.size(), true, insn_pos);
              code.insert(code.end(), 4, 0);
            }
          }
          if constexpr (std::is_same_v<T, multi_array_insn>) {
            const auto index = get_class(arg.desc);
            code.emplace_back(op::multianewarray);
            code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(index & 0xff));
            code.emplace_back(arg.dims);
          }
          if constexpr (std::is_same_v<T, array_insn>) {
            const auto& type = arg.type;
            if (const auto ty = std::get_if<std::string>(&type)) {
              const auto index = get_class(*ty);
              code.emplace_back(op::anewarray);
              code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
              code.emplace_back(static_cast<uint8_t>(index & 0xff));
            } else {
              const auto array_type = std::get<uint8_t>(type);
              code.emplace_back(op::newarray);
              code.emplace_back(array_type);
            }
          }
          if constexpr (std::is_same_v<T, invoke_dynamic_insn>) {
            const auto index = get_invoke_dynamic(arg.name, arg.desc, arg.handle, arg.args);
            code.emplace_back(op::invokedynamic);
            code.emplace_back(static_cast<uint8_t>((index >> 8) & 0xff));
            code.emplace_back(static_cast<uint8_t>(index & 0xff));
            code.emplace_back(0);
            code.emplace_back(0);
          }
        },
        i);
  }

  std::vector<int32_t> branch_offsets(branches.size());
  std::vector<uint8_t> pads(switch_pads.size());
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto& [label, pos, wide, insn_start] : branches) {
      if (code[insn_start] == op::goto_ || code[insn_start] == op::jsr) {
        const auto target = get_label(label);
        const auto offset = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(pos));
        if (offset > std::numeric_limits<int16_t>::max() || offset < std::numeric_limits<int16_t>::min()) {
          if (code[insn_start] == op::goto_) {
            code[insn_start] = op::goto_w;
          } else if (code[insn_start] == op::jsr) {
            code[insn_start] = op::jsr_w;
          }
          wide = true;
          code.insert(code.begin() + pos, 2, 0);
          shift(pos, 2);
          changed = true;
        }
      }
    }
    auto i = 0;
    for (const auto& pos : switch_pads) {
      const auto last_pad = pads[i];
      const auto padding = static_cast<uint8_t>((4 - (pos % 4)) % 4);
      if (padding != last_pad) {
        for (auto j = 0; j < last_pad; j++) {
          code.erase(code.begin() + pos);
          shift(pos, -1);
        }
        code.insert(code.begin() + pos, padding, 0);
        shift(pos, padding);
        pads[i] = padding;
        changed = true;
      }
      i++;
    }

    i = 0;
    for (const auto& [label, pos, wide, insn_start] : branches) {
      const auto target = get_label(label);
      if (wide) {
        const auto last_offset = branch_offsets[i];
        const auto offset = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(insn_start));
        branch_offsets[i] = offset;
        changed = changed || offset != last_offset;
        code[pos] = static_cast<int8_t>((offset >> 24) & 0xff);
        code[pos + 1] = static_cast<int8_t>((offset >> 16) & 0xff);
        code[pos + 2] = static_cast<int8_t>((offset >> 8) & 0xff);
        code[pos + 3] = static_cast<int8_t>(offset & 0xff);
      } else {
        const auto last_offset = branch_offsets[i];
        const auto offset = static_cast<int16_t>(static_cast<int32_t>(target) - static_cast<int32_t>(insn_start));
        branch_offsets[i] = offset;
        changed = changed || offset != last_offset;
        code[pos] = static_cast<int8_t>((offset >> 8) & 0xff);
        code[pos + 1] = static_cast<int8_t>(offset & 0xff);
      }
      i++;
    }
  }

  data_writer buf;
  if (oak) {
    buf.write_u8(static_cast<uint8_t>(max_stack));
    buf.write_u8(static_cast<uint8_t>(max_locals));
    buf.write_u16(static_cast<uint16_t>(code.size()));
  } else {
    buf.write_u16(max_stack);
    buf.write_u16(max_locals);
    buf.write_u32(static_cast<uint32_t>(code.size()));
  }
  buf.write_all(code);
  buf.write_u16(static_cast<uint16_t>(c.tcbs.size()));
  for (const auto& tcb : c.tcbs) {
    const auto start_pc = get_label(tcb.start);
    const auto end_pc = get_label(tcb.end);
    const auto handler_pc = get_label(tcb.handler);
    const auto catch_type = tcb.type ? get_class(*tcb.type) : 0;
    buf.write_u16(static_cast<uint16_t>(start_pc));
    buf.write_u16(static_cast<uint16_t>(end_pc));
    buf.write_u16(static_cast<uint16_t>(handler_pc));
    buf.write_u16(catch_type);
  }

  uint16_t attr_count = 0;
  data_writer attr_buf;
  if (!c.line_numbers.empty()) {
    const auto attr_name = get_utf("LineNumberTable");
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(c.line_numbers.size() * 4 + 2));
    attr_buf.write_u16(static_cast<uint16_t>(c.line_numbers.size()));
    for (const auto& [line, start] : c.line_numbers) {
      const auto start_pc = get_label(start);
      attr_buf.write_u16(static_cast<uint16_t>(start_pc));
      attr_buf.write_u16(line);
    }
    attr_count++;
  }

  data_writer local_buf;
  data_writer local_type_buf;
  uint16_t local_count = 0;
  uint16_t local_type_count = 0;
  for (const auto& local : c.locals) {
    const auto start_pc = get_label(local.start);
    const auto length = get_label(local.end) - start_pc;
    const auto name_index = get_utf(local.name);
    const auto desc_index = local.desc.empty() ? 0 : get_utf(local.desc);
    const auto signature_index = local.signature.empty() ? 0 : get_utf(local.signature);
    if (desc_index != 0) {
      local_count++;
      local_buf.write_u16(static_cast<uint16_t>(start_pc));
      local_buf.write_u16(static_cast<uint16_t>(length));
      local_buf.write_u16(name_index);
      local_buf.write_u16(desc_index);
      local_buf.write_u16(local.index);
    }

    if (signature_index != 0) {
      local_type_count++;
      local_type_buf.write_u16(static_cast<uint16_t>(start_pc));
      local_type_buf.write_u16(static_cast<uint16_t>(length));
      local_type_buf.write_u16(name_index);
      local_type_buf.write_u16(signature_index);
      local_type_buf.write_u16(local.index);
    }
  }
  if (local_count != 0) {
    const auto attr_name = get_utf("LocalVariableTable");
    const auto& data = local_buf.data();
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
    attr_buf.write_u16(local_count);
    attr_buf.write_all(data);
    attr_count++;
  }
  if (local_type_count != 0) {
    const auto attr_name = get_utf("LocalVariableTypeTable");
    const auto& data = local_type_buf.data();
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
    attr_buf.write_u16(local_type_count);
    attr_buf.write_all(data);
    attr_count++;
  }
  if (!frames.empty()) {
    const auto attr_name = get_utf("StackMapTable");
    data_writer frame_buf;
    attr_buf.write_u16(attr_name);
    frame_buf.write_u16(static_cast<uint16_t>(frames.size()));
    for (const auto& [target, frame] : frames) {
      const auto delta = next_delta(target);
      std::visit(
          [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, same_frame>) {
              if (const auto stack_opt = arg.stack) {
                if (delta <= 63) {
                  frame_buf.write_u8(static_cast<uint8_t>(delta + 64));
                } else {
                  frame_buf.write_u8(247);
                  frame_buf.write_u16(delta);
                }
                write_frame_var(frame_buf, *stack_opt);
              } else {
                if (delta <= 63) {
                  frame_buf.write_u8(static_cast<uint8_t>(delta));
                } else {
                  frame_buf.write_u8(251);
                  frame_buf.write_u16(delta);
                }
              }
            } else if constexpr (std::is_same_v<T, full_frame>) {
              frame_buf.write_u8(255);
              frame_buf.write_u16(delta);
              frame_buf.write_u16(static_cast<uint16_t>(arg.locals.size()));
              for (const auto& local : arg.locals) {
                write_frame_var(frame_buf, local);
              }
              frame_buf.write_u16(static_cast<uint16_t>(arg.stack.size()));
              for (const auto& stack : arg.stack) {
                write_frame_var(frame_buf, stack);
              }
            } else if constexpr (std::is_same_v<T, chop_frame>) {
              frame_buf.write_u8(static_cast<uint8_t>(251 - arg.size));
              frame_buf.write_u16(delta);
            } else if constexpr (std::is_same_v<T, append_frame>) {
              frame_buf.write_u8(static_cast<uint8_t>(arg.locals.size() + 251));
              frame_buf.write_u16(delta);
              for (const auto& local : arg.locals) {
                write_frame_var(frame_buf, local);
              }
            }
          },
          frame);
    }
    const auto data = frame_buf.data();
    attr_buf.write_u32(static_cast<uint32_t>(data.size()));
    attr_buf.write_all(data);
    attr_count++;
  }
  if (!c.visible_type_annotations.empty()) {
    const auto attr_name = get_utf("RuntimeVisibleTypeAnnotations");
    const auto& data = get_type_annotations(c.visible_type_annotations, labels);
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
    attr_buf.write_u16(static_cast<uint16_t>(c.visible_type_annotations.size()));
    attr_buf.write_all(data);
    attr_count++;
  }
  if (!c.invisible_type_annotations.empty()) {
    const auto attr_name = get_utf("RuntimeInvisibleTypeAnnotations");
    const auto& data = get_type_annotations(c.invisible_type_annotations, labels);
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(data.size() + 2));
    attr_buf.write_u16(static_cast<uint16_t>(c.invisible_type_annotations.size()));
    attr_buf.write_all(data);
    attr_count++;
  }
  for (const auto& attr : c.attributes) {
    const auto attr_name = get_utf(attr.name);
    attr_buf.write_u16(attr_name);
    attr_buf.write_u32(static_cast<uint32_t>(attr.data.size()));
    attr_buf.write_all(attr.data);
    attr_count++;
  }
  buf.write_u16(attr_count);
  buf.write_all(attr_buf.data());
  return buf.data();
}
uint16_t class_writer::get_class(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto cls = std::get_if<cp::class_info>(&pool_.at(i))) {
      if (cls->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::class_info{utf_index});
  return static_cast<uint32_t>(index);
}
uint16_t class_writer::get_field_ref(const std::string_view& owner, const std::string_view& name,
                                     const std::string_view& desc) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto field = std::get_if<cp::field_ref_info>(&pool_.at(i))) {
      if (field->class_index == class_index && field->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::field_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_ref(const std::string_view& owner, const std::string_view& name,
                                      const std::string_view& desc) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto method = std::get_if<cp::method_ref_info>(&pool_.at(i))) {
      if (method->class_index == class_index && method->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_interface_method_ref(const std::string_view& owner, const std::string_view& name,
                                                const std::string_view& desc) {
  const auto class_index = get_class(owner);
  const auto nat_index = get_name_and_type(name, desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto method = std::get_if<cp::interface_method_ref_info>(&pool_.at(i))) {
      if (method->class_index == class_index && method->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::interface_method_ref_info{class_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_string(const std::string_view& str) {
  const auto utf_index = get_utf(str);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto string = std::get_if<cp::string_info>(&pool_.at(i))) {
      if (string->string_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::string_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_int(int32_t value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto integer = std::get_if<cp::integer_info>(&pool_.at(i))) {
      if (integer->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::integer_info{value});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_float(float value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto flt = std::get_if<cp::float_info>(&pool_.at(i))) {
      if (flt->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::float_info{value});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_long(int64_t value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto lng = std::get_if<cp::long_info>(&pool_.at(i))) {
      if (lng->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::long_info{value});
  pool_.emplace_back(cp::pad_info{});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_double(double value) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto dbl = std::get_if<cp::double_info>(&pool_.at(i))) {
      if (dbl->value == value) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::double_info{value});
  pool_.emplace_back(cp::pad_info{});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_name_and_type(const std::string_view& name, const std::string_view& desc) {
  const auto name_index = get_utf(name);
  const auto desc_index = get_utf(desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto nat = std::get_if<cp::name_and_type_info>(&pool_.at(i))) {
      if (nat->name_index == name_index && nat->desc_index == desc_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::name_and_type_info{name_index, desc_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_utf(const std::string_view& utf) {
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto str = std::get_if<cp::utf8_info>(&pool_.at(i))) {
      if (str->value == utf) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::utf8_info{std::string(utf)});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_handle(uint8_t kind, const std::string_view& owner, const std::string_view& name,
                                         const std::string_view& desc, bool interface) {
  uint16_t reference_index;
  switch (kind) {
    case reference_kind::get_field:
    case reference_kind::get_static:
    case reference_kind::put_field:
    case reference_kind::put_static:
      reference_index = get_field_ref(owner, name, desc);
      break;
      break;
    default:
      reference_index = interface ? get_interface_method_ref(owner, name, desc) : get_method_ref(owner, name, desc);
      break;
  }
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto handle = std::get_if<cp::method_handle_info>(&pool_.at(i))) {
      if (handle->reference_kind == kind && handle->reference_index == reference_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_handle_info{kind, reference_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_method_type(const std::string_view& desc) {
  const auto utf_index = get_utf(desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto type = std::get_if<cp::method_type_info>(&pool_.at(i))) {
      if (type->desc_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::method_type_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_dynamic(const std::string_view& name, const std::string_view& desc,
                                   const method_handle& handle, const std::vector<value>& args) {
  const auto bsm_index = get_bsm(handle, args);
  const auto nat_index = get_name_and_type(name, desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto dynamic = std::get_if<cp::dynamic_info>(&pool_.at(i))) {
      if (dynamic->bootstrap_method_attr_index == bsm_index && dynamic->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::dynamic_info{bsm_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_invoke_dynamic(const std::string_view& name, const std::string_view& desc,
                                          const method_handle& handle, const std::vector<value>& args) {
  const auto bsm_index = get_bsm(handle, args);
  const auto nat_index = get_name_and_type(name, desc);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto invoke = std::get_if<cp::invoke_dynamic_info>(&pool_.at(i))) {
      if (invoke->bootstrap_method_attr_index == bsm_index && invoke->name_and_type_index == nat_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::invoke_dynamic_info{bsm_index, nat_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_module(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto module = std::get_if<cp::module_info>(&pool_.at(i))) {
      if (module->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::module_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_package(const std::string_view& name) {
  const auto utf_index = get_utf(name);
  for (uint16_t i = 1; i < static_cast<uint16_t>(pool_.size()); i++) {
    if (const auto package = std::get_if<cp::package_info>(&pool_.at(i))) {
      if (package->name_index == utf_index) {
        return i;
      }
    }
  }
  const auto index = pool_.size();
  pool_.emplace_back(cp::package_info{utf_index});
  return static_cast<uint16_t>(index);
}
uint16_t class_writer::get_value(const value& val) {
  return std::visit(
      [this](const auto& arg) -> uint16_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int32_t>) {
          return get_int(arg);
        } else if constexpr (std::is_same_v<T, float>) {
          return get_float(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return get_long(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          return get_double(arg);
        } else if constexpr (std::is_same_v<T, type>) {
          return get_class(arg.get());
        } else if constexpr (std::is_same_v<T, std::string>) {
          return get_string(arg);
        } else if constexpr (std::is_same_v<T, method_handle>) {
          return get_method_handle(arg.kind, arg.owner, arg.name, arg.desc, arg.interface);
        } else if constexpr (std::is_same_v<T, method_type>) {
          return get_method_type(arg.desc);
        } else if constexpr (std::is_same_v<T, dynamic>) {
          return get_dynamic(arg.name, arg.desc, arg.handle, arg.args);
        }
      },
      val);
}
uint16_t class_writer::get_bsm(const method_handle& handle, const std::vector<value>& args) {
  const auto handle_index = get_method_handle(handle.kind, handle.owner, handle.name, handle.desc, handle.interface);
  std::vector<uint16_t> arg_indices;
  arg_indices.reserve(args.size());
  for (const auto& arg : args) {
    arg_indices.emplace_back(get_value(arg));
  }
  for (uint16_t i = 0; i < bsm_buffer_.size(); i++) {
    if (bsm_buffer_[i].first == handle_index && bsm_buffer_[i].second == arg_indices) {
      return i;
    }
  }
  const auto index = bsm_buffer_.size();
  bsm_buffer_.emplace_back(handle_index, arg_indices);
  return static_cast<uint16_t>(index);
}
std::vector<int8_t> class_writer::get_annotations(const std::vector<annotation>& annos) {
  data_writer buf;
  for (const auto& anno : annos) {
    const auto type_index = get_utf(anno.desc);
    buf.write_u16(type_index);
    buf.write_u16(static_cast<uint16_t>(anno.values.size()));
    for (const auto& [name, value] : anno.values) {
      const auto element_name_index = get_utf(name);
      buf.write_u16(element_name_index);
      get_element_value(buf, value);
    }
  }
  return buf.data();
}
void class_writer::get_element_value(data_writer& buf, const element_value& value) {
  std::visit(
      [this, &buf](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int8_t>) {
          buf.write_u8('B');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          buf.write_u8('C');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, double>) {
          buf.write_u8('D');
          buf.write_u16(get_double(arg));
        } else if constexpr (std::is_same_v<T, float>) {
          buf.write_u8('F');
          buf.write_u16(get_float(arg));
        } else if constexpr (std::is_same_v<T, int32_t>) {
          buf.write_u8('I');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, int64_t>) {
          buf.write_u8('J');
          buf.write_u16(get_long(arg));
        } else if constexpr (std::is_same_v<T, int16_t>) {
          buf.write_u8('S');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, bool>) {
          buf.write_u8('Z');
          buf.write_u16(get_int(arg));
        } else if constexpr (std::is_same_v<T, std::string>) {
          buf.write_u8('s');
          buf.write_u16(get_utf(arg));
        } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
          buf.write_u8('e');
          buf.write_u16(get_utf(arg.first));
          buf.write_u16(get_utf(arg.second));
        } else if constexpr (std::is_same_v<T, type>) {
          buf.write_u8('c');
          buf.write_u16(get_utf(arg.get()));
        } else if constexpr (std::is_same_v<T, annotation>) {
          buf.write_u8('@');
          buf.write_all(get_annotations({arg}));
        } else if constexpr (std::is_same_v<T, std::vector<element_value>>) {
          buf.write_u8('[');
          buf.write_u16(static_cast<uint16_t>(arg.size()));
          for (const auto& elem : arg) {
            get_element_value(buf, elem);
          }
        }
      },
      value.value);
}
std::vector<int8_t> class_writer::get_type_annotations(const std::vector<type_annotation>& annos,
                                                       const std::vector<std::pair<size_t, label>>& labels) {
  const auto get_label = [&labels](const label& lbl) -> size_t {
    for (const auto& [idx, label] : labels) {
      if (lbl == label) {
        return idx;
      }
    }
    return 0;
  };
  data_writer buf;
  for (const auto& anno : annos) {
    buf.write_u8(anno.target_type);
    std::visit(
        [this, &buf, &get_label](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, target::type_parameter>) {
            buf.write_u8(arg.index);
          } else if constexpr (std::is_same_v<T, target::supertype>) {
            buf.write_u16(arg.index);
          } else if constexpr (std::is_same_v<T, target::type_parameter_bound>) {
            buf.write_u8(arg.type_parameter_index);
            buf.write_u8(arg.bound_index);
          } else if constexpr (std::is_same_v<T, target::empty>) {
            // Do nothing
          } else if constexpr (std::is_same_v<T, target::formal_parameter>) {
            buf.write_u8(arg.index);
          } else if constexpr (std::is_same_v<T, target::throws>) {
            buf.write_u16(arg.index);
          } else if constexpr (std::is_same_v<T, target::localvar>) {
            buf.write_u16(static_cast<uint16_t>(arg.table.size()));
            for (const auto& local : arg.table) {
              const auto start_pc = get_label(local.start);
              const auto end_pc = get_label(local.end);
              const auto length = end_pc - start_pc;
              buf.write_u16(static_cast<uint16_t>(start_pc));
              buf.write_u16(static_cast<uint16_t>(length));
              buf.write_u16(local.index);
            }
          } else if constexpr (std::is_same_v<T, target::catch_target>) {
            buf.write_u16(arg.index);
          } else if constexpr (std::is_same_v<T, target::offset_target>) {
            const auto offset_pc = get_label(arg.offset);
            buf.write_u16(static_cast<uint16_t>(offset_pc));
          } else if constexpr (std::is_same_v<T, target::type_argument>) {
            const auto offset_pc = get_label(arg.offset);
            buf.write_u16(static_cast<uint16_t>(offset_pc));
            buf.write_u8(arg.index);
          }
        },
        anno.target_info);
    buf.write_u8(static_cast<uint8_t>(anno.target_path.path.size()));
    for (const auto& [kind, index] : anno.target_path.path) {
      buf.write_u8(kind);
      buf.write_u8(index);
    }
    const auto type_index = get_utf(anno.desc);
    buf.write_u16(type_index);
    buf.write_u16(static_cast<uint16_t>(anno.values.size()));
    for (const auto& [name, value] : anno.values) {
      const auto element_name_index = get_utf(name);
      buf.write_u16(element_name_index);
      get_element_value(buf, value);
    }
  }
  return buf.data();
}
std::vector<int8_t> class_writer::write(const class_file& file) {
  pool_.clear();
  bsm_buffer_.clear();
  attributes_.clear();
  attributes_count_ = 0;
  fields_.clear();
  methods_.clear();
  pool_.emplace_back(cp::pad_info{});
  uint16_t this_class = get_class(file.name);
  uint16_t super_class = file.super_name ? get_class(*file.super_name) : 0;
  std::vector<uint16_t> interfaces;
  interfaces.reserve(file.interfaces.size());
  for (const auto& i : file.interfaces) {
    interfaces.emplace_back(get_class(i));
  }
  if (file.source_file) {
    write_source_file(*file.source_file);
  }
  if (file.signature) {
    write_signature(*file.signature);
  }
  if (!file.inner_classes.empty()) {
    write_inner_classes(file.inner_classes);
  }
  if (file.enclosing_method) {
    const auto& [owner, method] = *file.enclosing_method;
    write_enclosing_method(owner, method);
  }
  if (file.source_debug_extension) {
    write_source_debug_extension(*file.source_debug_extension);
  }
  if (file.module) {
    write_module(*file.module);
  }
  if (!file.module_packages.empty()) {
    write_module_packages(file.module_packages);
  }
  if (file.module_main_class) {
    write_module_main_class(*file.module_main_class);
  }
  if (file.nest_host) {
    write_nest_host(*file.nest_host);
  }
  if (!file.nest_members.empty()) {
    write_nest_members(file.nest_members);
  }
  if (!file.record_components.empty()) {
    write_record(file.record_components);
  }
  if (!file.permitted_subclasses.empty()) {
    write_permitted_subclasses(file.permitted_subclasses);
  }
  if (file.synthetic) {
    databuf buf(attributes_);
    buf.write_u16(get_utf("Synthetic"));
    buf.write_u32(0);
    attributes_count_++;
  }
  if (file.deprecated) {
    databuf buf(attributes_);
    buf.write_u16(get_utf("Deprecated"));
    buf.write_u32(0);
    attributes_count_++;
  }
  if (!file.visible_annotations.empty()) {
    write_annotations(file.visible_annotations, true);
  }
  if (!file.invisible_annotations.empty()) {
    write_annotations(file.invisible_annotations, false);
  }
  if (!file.visible_type_annotations.empty()) {
    write_type_annotations(file.visible_type_annotations, true);
  }
  if (!file.invisible_type_annotations.empty()) {
    write_type_annotations(file.invisible_type_annotations, false);
  }
  for (const auto& attr : file.attributes) {
    const auto attr_name = get_utf(attr.name);
    databuf buf(attributes_);
    buf.write_u16(attr_name);
    buf.write_u16(static_cast<uint16_t>(attr.data.size()));
    buf.write_all(attr.data);
    attributes_count_++;
  }
  write_fields(file.fields);
  write_methods(file, file.methods, file.version < class_version::v1_1);

  if (!bsm_buffer_.empty()) {
    const auto attr_name = get_utf("BootstrapMethods");
    data_writer attrbuf;
    attrbuf.write_u16(static_cast<uint16_t>(bsm_buffer_.size()));
    for (const auto& [ref, args] : bsm_buffer_) {
      attrbuf.write_u16(ref);
      attrbuf.write_u16(static_cast<uint16_t>(args.size()));
      for (const auto& i : args) {
        attrbuf.write_u16(i);
      }
    }
    const auto attr = attrbuf.data();
    databuf buf(attributes_);
    buf.write_u16(attr_name);
    buf.write_u32(static_cast<uint32_t>(attr.size()));
    buf.write_all(attr);
    attributes_count_++;
  }

  data_writer buf;
  buf.write_u32(0xcafebabe);
  buf.write_u16(static_cast<uint16_t>(file.version & 0xffff));
  buf.write_u16(static_cast<uint16_t>(file.version >> 16));
  uint16_t pool_count = 1;
  for (const auto& info : pool_) {
    if (std::holds_alternative<cp::pad_info>(info)) {
      continue;
    }
    pool_count++;
    if (std::holds_alternative<cp::long_info>(info) || std::holds_alternative<cp::double_info>(info)) {
      pool_count++;
    }
  }
  buf.write_u16(pool_count);
  for (const auto& info : pool_) {
    std::visit(constant_pool_visitor(buf), info);
  }
  buf.write_u16(file.access_flags);
  buf.write_u16(this_class);
  buf.write_u16(super_class);
  buf.write_u16(static_cast<uint16_t>(interfaces.size()));
  for (const auto& i : interfaces) {
    buf.write_u16(i);
  }
  buf.write_u16(static_cast<uint16_t>(file.fields.size()));
  buf.write_all(fields_);
  buf.write_u16(static_cast<uint16_t>(file.methods.size()));
  buf.write_all(methods_);
  buf.write_u16(attributes_count_);
  buf.write_all(attributes_);
  return buf.data();
}
} // namespace cafe
