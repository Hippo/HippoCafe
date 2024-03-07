#include "visitor.hpp"

#include <sstream>

namespace cafe {

void write_annotation(data_writer& writer, const attribute::annotation& anno);

void write_element_value(data_writer& writer, const attribute::element_value& value) {
  writer.write_u8(value.tag);
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, uint16_t>) {
          writer.write_u16(v);
        } else if constexpr (std::is_same_v<T, attribute::element_value::enum_value>) {
          writer.write_u16(v.type_name_index);
          writer.write_u16(v.const_name_index);
        } else if constexpr (std::is_same_v<T, attribute::annotation>) {
          write_annotation(writer, v);
        } else if constexpr (std::is_same_v<T, std::vector<attribute::element_value>>) {
          writer.write_u16(static_cast<uint16_t>(v.size()));
          for (const auto& e : v) {
            write_element_value(writer, e);
          }
        }
      },
      value.value);
}

void write_annotation(data_writer& writer, const attribute::annotation& anno) {
  writer.write_u16(anno.type_index);
  writer.write_u16(static_cast<uint16_t>(anno.elements.size()));
  for (const auto& e : anno.elements) {
    writer.write_u16(e.name_index);
    write_element_value(writer, e.value);
  }
}

void write_type_annotation(data_writer& writer, const attribute::type_annotation& anno) {
  writer.write_u8(anno.target_type);
  std::visit(
      [&](const auto& t) {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, attribute::type_parameter>) {
          writer.write_u8(t.index);
        } else if constexpr (std::is_same_v<T, attribute::supertype>) {
          writer.write_u16(t.index);
        } else if constexpr (std::is_same_v<T, attribute::formal_parameter>) {
          writer.write_u8(t.index);
        } else if constexpr (std::is_same_v<T, attribute::throws>) {
          writer.write_u16(t.index);
        } else if constexpr (std::is_same_v<T, attribute::localvar>) {
          writer.write_u16(static_cast<uint16_t>(t.table.size()));
          for (const auto& l : t.table) {
            writer.write_u16(l.start_pc);
            writer.write_u16(l.length);
            writer.write_u16(l.index);
          }
        } else if constexpr (std::is_same_v<T, attribute::catch_target>) {
          writer.write_u16(t.index);
        } else if constexpr (std::is_same_v<T, attribute::offset_target>) {
          writer.write_u16(t.offset);
        } else if constexpr (std::is_same_v<T, attribute::type_argument>) {
          writer.write_u16(t.offset);
          writer.write_u8(t.index);
        }
      },
      anno.target_info);
  writer.write_u16(static_cast<uint16_t>(anno.target_path.paths.size()));
  for (const auto& [kind, index] : anno.target_path.paths) {
    writer.write_u8(kind);
    writer.write_u8(index);
  }
  writer.write_u16(anno.type_index);
  writer.write_u16(static_cast<uint16_t>(anno.elements.size()));
  for (const auto& e : anno.elements) {
    writer.write_u16(e.name_index);
    write_element_value(writer, e.value);
  }
}

constant_pool_visitor::constant_pool_visitor(data_writer& writer) : writer_(writer) {
}
void constant_pool_visitor::operator()(const cp::pad_info& info) const {
}

void constant_pool_visitor::operator()(const cp::class_info& info) {
  writer_.write_u8(cp::class_info::tag);
  writer_.write_u16(info.name_index);
}

void constant_pool_visitor::operator()(const cp::field_ref_info& info) {
  writer_.write_u8(cp::field_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::method_ref_info& info) {
  writer_.write_u8(cp::method_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::interface_method_ref_info& info) {
  writer_.write_u8(cp::interface_method_ref_info::tag);
  writer_.write_u16(info.class_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::string_info& info) {
  writer_.write_u8(cp::string_info::tag);
  writer_.write_u16(info.string_index);
}

void constant_pool_visitor::operator()(const cp::integer_info& info) {
  writer_.write_u8(cp::integer_info::tag);
  writer_.write_u32(static_cast<uint32_t>(info.value));
}

void constant_pool_visitor::operator()(const cp::float_info& info) {
  writer_.write_u8(cp::float_info::tag);
  writer_.write_f32(info.value);
}

void constant_pool_visitor::operator()(const cp::long_info& info) {
  writer_.write_u8(cp::long_info::tag);
  writer_.write_u64(static_cast<uint64_t>(info.value));
}

void constant_pool_visitor::operator()(const cp::double_info& info) {
  writer_.write_u8(cp::double_info::tag);
  writer_.write_f64(info.value);
}

void constant_pool_visitor::operator()(const cp::name_and_type_info& info) {
  writer_.write_u8(cp::name_and_type_info::tag);
  writer_.write_u16(info.name_index);
  writer_.write_u16(info.descriptor_index);
}

void constant_pool_visitor::operator()(const cp::utf8_info& info) {
  writer_.write_u8(cp::utf8_info::tag);
  writer_.write_utf(info.value);
}

void constant_pool_visitor::operator()(const cp::method_handle_info& info) {
  writer_.write_u8(cp::method_handle_info::tag);
  writer_.write_u8(info.reference_kind);
  writer_.write_u16(info.reference_index);
}

void constant_pool_visitor::operator()(const cp::method_type_info& info) {
  writer_.write_u8(cp::method_type_info::tag);
  writer_.write_u16(info.descriptor_index);
}

void constant_pool_visitor::operator()(const cp::dynamic_info& info) {
  writer_.write_u8(cp::dynamic_info::tag);
  writer_.write_u16(info.bootstrap_method_attr_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::invoke_dynamic_info& info) {
  writer_.write_u8(cp::invoke_dynamic_info::tag);
  writer_.write_u16(info.bootstrap_method_attr_index);
  writer_.write_u16(info.name_and_type_index);
}

void constant_pool_visitor::operator()(const cp::module_info& info) {
  writer_.write_u8(cp::module_info::tag);
  writer_.write_u16(info.name_index);
}

void constant_pool_visitor::operator()(const cp::package_info& info) {
  writer_.write_u8(cp::package_info::tag);
  writer_.write_u16(info.name_index);
}

attribute_visitor::attribute_visitor(data_writer& writer, const cp::constant_pool& pool, bool oak) :
    writer_(writer), pool_(pool), oak_(oak) {
}

void attribute_visitor::write_name(const std::string_view& name) {
  for (auto i = 0; i < pool_.size(); i++) {
    if (const auto utf = std::get_if<cp::utf8_info>(&pool_[i])) {
      if (utf->value == name) {
        writer_.write_u16(i);
        return;
      }
    }
  }
  throw std::runtime_error("Could not find attribute name '" + std::string(name) + "'");
}

void attribute_visitor::operator()(const attribute::unknown& attr) {
  writer_.write_u16(attr.name_index);
  writer_.write_u32(static_cast<uint32_t>(attr.info.size()));
  writer_.write_bytes(attr.info);
}
void attribute_visitor::operator()(const attribute::constant_value& attr) {
  write_name("ConstantValue");
  writer_.write_u32(2);
  writer_.write_u16(attr.index);
}
void attribute_visitor::operator()(const attribute::code& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  oak_ ? writer.write_u8(static_cast<uint8_t>(attr.max_stack)) : writer.write_u16(attr.max_stack);
  oak_ ? writer.write_u8(static_cast<uint8_t>(attr.max_locals)) : writer.write_u16(attr.max_locals);
  oak_ ? writer.write_u16(static_cast<uint16_t>(attr.bytecode.size()))
       : writer.write_u32(static_cast<uint32_t>(attr.bytecode.size()));
  writer.write_bytes(attr.bytecode);
  writer.write_u16(static_cast<uint16_t>(attr.exceptions.size()));
  for (const auto& e : attr.exceptions) {
    writer.write_u16(e.start_pc);
    writer.write_u16(e.end_pc);
    writer.write_u16(e.handler_pc);
    writer.write_u16(e.catch_type);
  }
  writer.write_u16(static_cast<uint16_t>(attr.attributes.size()));
  for (const auto& a : attr.attributes) {
    std::visit(attribute_visitor(writer, pool_, oak_), a);
  }
  const auto bytes = oss.str();
  write_name("Code");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::stack_map_table& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.entries.size()));
  for (const auto& frame : attr.entries) {
    std::visit(
        [&](const auto& f) {
          using T = std::decay_t<decltype(f)>;
          const auto verification_visitor = [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            const auto tag = T::tag;
            writer.write_u8(tag);
            if constexpr (std::is_same_v<T, attribute::object_variable_info>) {
              writer.write_u16(v.index);
            } else if constexpr (std::is_same_v<T, attribute::uninitialized_variable_info>) {
              writer.write_u16(v.offset);
            }
          };

          if constexpr (std::is_same_v<T, attribute::same_frame>) {
            writer.write_u8(f.frame_type);
          } else if constexpr (std::is_same_v<T, attribute::same_locals_1_stack_item_frame>) {
            writer.write_u8(f.frame_type);
            std::visit(verification_visitor, f.stack);
          } else if constexpr (std::is_same_v<T, attribute::same_locals_1_stack_item_frame_extended>) {
            writer.write_u8(T::frame_type);
            writer.write_u16(f.offset_delta);
            std::visit(verification_visitor, f.stack);
          } else if constexpr (std::is_same_v<T, attribute::chop_frame>) {
            writer.write_u8(f.frame_type);
            writer.write_u16(f.offset_delta);
          } else if constexpr (std::is_same_v<T, attribute::same_frame_extended>) {
            writer.write_u8(T::frame_type);
            writer.write_u16(f.offset_delta);
          } else if constexpr (std::is_same_v<T, attribute::append_frame>) {
            writer.write_u8(f.frame_type);
            writer.write_u16(f.offset_delta);
            for (const auto& l : f.locals) {
              std::visit(verification_visitor, l);
            }
          } else if constexpr (std::is_same_v<T, attribute::full_frame>) {
            writer.write_u8(T::frame_type);
            writer.write_u16(f.offset_delta);
            writer.write_u16(static_cast<uint16_t>(f.locals.size()));
            for (const auto& l : f.locals) {
              std::visit(verification_visitor, l);
            }
            writer.write_u16(static_cast<uint16_t>(f.stack.size()));
            for (const auto& s : f.stack) {
              std::visit(verification_visitor, s);
            }
          }
        },
        frame);
  }
  const auto bytes = oss.str();
  write_name("StackMapTable");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::exceptions& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.exception_index_table.size()));
  for (const auto& e : attr.exception_index_table) {
    writer.write_u16(e);
  }
  const auto bytes = oss.str();
  write_name("Exceptions");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::inner_classes& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.classes.size()));
  for (const auto& c : attr.classes) {
    writer.write_u16(c.inner_index);
    writer.write_u16(c.outer_index);
    writer.write_u16(c.name_index);
    writer.write_u16(c.access_flags);
  }
  const auto bytes = oss.str();
  write_name("InnerClasses");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::enclosing_method& attr) {
  write_name("Enclosingmethod");
  writer_.write_u32(4);
  writer_.write_u16(attr.class_index);
  writer_.write_u16(attr.method_index);
}
void attribute_visitor::operator()(const attribute::synthetic& attr) {
  write_name("Synthetic");
  writer_.write_u32(0);
}
void attribute_visitor::operator()(const attribute::signature& attr) {
  write_name("Signature");
  writer_.write_u32(2);
  writer_.write_u16(attr.index);
}
void attribute_visitor::operator()(const attribute::source_file& attr) {
  write_name("SourceFile");
  writer_.write_u32(2);
  writer_.write_u16(attr.index);
}
void attribute_visitor::operator()(const attribute::source_debug_extension& attr) {
  write_name("SourceDebugExtension");
  writer_.write_u32(static_cast<uint32_t>(attr.debug_extension.size()));
  writer_.write_bytes(attr.debug_extension);
}
void attribute_visitor::operator()(const attribute::line_number_table& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.line_numbers.size()));
  for (const auto& l : attr.line_numbers) {
    writer.write_u16(l.start_pc);
    writer.write_u16(l.number);
  }
  const auto bytes = oss.str();
  write_name("LineNumberTable");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::local_variable_table& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.local_variables.size()));
  for (const auto& l : attr.local_variables) {
    writer.write_u16(l.start_pc);
    writer.write_u16(l.length);
    writer.write_u16(l.name_index);
    writer.write_u16(l.descriptor_index);
    writer.write_u16(l.index);
  }
  const auto bytes = oss.str();
  write_name("LocalVariableTable");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::local_variable_type_table& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.local_variables.size()));
  for (const auto& l : attr.local_variables) {
    writer.write_u16(l.start_pc);
    writer.write_u16(l.length);
    writer.write_u16(l.name_index);
    writer.write_u16(l.signature_index);
    writer.write_u16(l.index);
  }
  const auto bytes = oss.str();
  write_name("LocalVariableTypeTable");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::deprecated& attr) {
  write_name("Deprecated");
  writer_.write_u32(0);
}
void attribute_visitor::operator()(const attribute::runtime_visible_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.annotations.size()));
  for (const auto& a : attr.annotations) {
    write_annotation(writer, a);
  }
  const auto bytes = oss.str();
  write_name("RuntimeVisibleAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::runtime_invisible_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.annotations.size()));
  for (const auto& a : attr.annotations) {
    write_annotation(writer, a);
  }
  const auto bytes = oss.str();
  write_name("RuntimeInvisibleAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::runtime_visible_parameter_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u8(static_cast<uint8_t>(attr.parameter_annotations.size()));
  for (const auto& p : attr.parameter_annotations) {
    writer.write_u16(static_cast<uint16_t>(p.size()));
    for (const auto& a : p) {
      write_annotation(writer, a);
    }
  }
  const auto bytes = oss.str();
  write_name("RuntimeVisibleParameterAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::runtime_invisible_parameter_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u8(static_cast<uint8_t>(attr.parameter_annotations.size()));
  for (const auto& p : attr.parameter_annotations) {
    writer.write_u16(static_cast<uint16_t>(p.size()));
    for (const auto& a : p) {
      write_annotation(writer, a);
    }
  }
  const auto bytes = oss.str();
  write_name("RuntimeInvisibleParameterAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::runtime_visible_type_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.annotations.size()));
  for (const auto& a : attr.annotations) {
    write_type_annotation(writer, a);
  }
  const auto bytes = oss.str();
  write_name("RuntimeVisibleTypeAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::runtime_invisible_type_annotations& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.annotations.size()));
  for (const auto& a : attr.annotations) {
    write_type_annotation(writer, a);
  }
  const auto bytes = oss.str();
  write_name("RuntimeInvisibleTypeAnnotations");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::annotation_default& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  write_element_value(writer, attr.default_value);
  const auto bytes = oss.str();
  write_name("AnnotationDefault");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::bootstrap_methods& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.methods.size()));
  for (const auto& m : attr.methods) {
    writer.write_u16(m.index);
    writer.write_u16(static_cast<uint16_t>(m.arguments.size()));
    for (const auto& a : m.arguments) {
      writer.write_u16(a);
    }
  }
  const auto bytes = oss.str();
  write_name("BootstrapMethods");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::method_parameters& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u8(static_cast<uint8_t>(attr.parameters.size()));
  for (const auto& p : attr.parameters) {
    writer.write_u16(p.name_index);
    writer.write_u16(p.access_flags);
  }
  const auto bytes = oss.str();
  write_name("MethodParameters");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::module& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(attr.module_name_index);
  writer.write_u16(attr.module_flags);
  writer.write_u16(attr.module_version_index);

  writer.write_u16(static_cast<uint16_t>(attr.requires.size()));
  for (const auto& r : attr.requires) {
    writer.write_u16(r.require_index);
    writer.write_u16(r.require_flags);
    writer.write_u16(r.require_version_index);
  }

  writer.write_u16(static_cast<uint16_t>(attr.exports.size()));
  for (const auto& e : attr.exports) {
    writer.write_u16(e.exports_index);
    writer.write_u16(e.exports_flags);
    writer.write_u16(static_cast<uint16_t>(e.exports_to_index.size()));
    for (const auto& t : e.exports_to_index) {
      writer.write_u16(t);
    }
  }

  writer.write_u16(static_cast<uint16_t>(attr.opens.size()));
  for (const auto& o : attr.opens) {
    writer.write_u16(o.opens_index);
    writer.write_u16(o.opens_flags);
    writer.write_u16(static_cast<uint16_t>(o.opens_to_index.size()));
    for (const auto& t : o.opens_to_index) {
      writer.write_u16(t);
    }
  }

  writer.write_u16(static_cast<uint16_t>(attr.uses.size()));
  for (const auto& u : attr.uses) {
    writer.write_u16(u);
  }

  writer.write_u16(static_cast<uint16_t>(attr.provides.size()));
  for (const auto& p : attr.provides) {
    writer.write_u16(p.provides_index);
    writer.write_u16(static_cast<uint16_t>(p.provides_with_index.size()));
    for (const auto& w : p.provides_with_index) {
      writer.write_u16(w);
    }
  }

  const auto bytes = oss.str();
  write_name("Module");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::module_packages& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.package_indices.size()));
  for (const auto& p : attr.package_indices) {
    writer.write_u16(p);
  }
  const auto bytes = oss.str();
  write_name("ModulePackages");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::module_main_class& attr) {
  write_name("ModuleMainClass");
  writer_.write_u32(2);
  writer_.write_u16(attr.index);
}
void attribute_visitor::operator()(const attribute::nest_host& attr) {
  write_name("NestHost");
  writer_.write_u32(2);
  writer_.write_u16(attr.index);
}
void attribute_visitor::operator()(const attribute::nest_members& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.classes.size()));
  for (const auto& c : attr.classes) {
    writer.write_u16(c);
  }
  const auto bytes = oss.str();
  write_name("NestMembers");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::record& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.components.size()));
  for (const auto& c : attr.components) {
    writer.write_u16(c.name_index);
    writer.write_u16(c.descriptor_index);
    writer.write_u16(static_cast<uint16_t>(c.attributes.size()));
    for (const auto& a : c.attributes) {
      std::visit(attribute_visitor(writer, pool_, oak_), a);
    }
  }
  const auto bytes = oss.str();
  write_name("Record");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
void attribute_visitor::operator()(const attribute::permitted_subclasses& attr) {
  std::ostringstream oss(std::ios::binary);
  data_writer writer(oss);
  writer.write_u16(static_cast<uint16_t>(attr.classes.size()));
  for (const auto& c : attr.classes) {
    writer.write_u16(c);
  }
  const auto bytes = oss.str();
  write_name("PermittedSubclasses");
  writer_.write_u32(static_cast<uint32_t>(bytes.size()));
  writer_.write_bytes(bytes);
}
} // namespace cafe
