#include "cafe/class_reader.hpp"

#include <iostream>

#include "cafe/constants.hpp"


namespace cafe {

result<class_file> class_reader::read(data_reader&& reader) {
  class_file file;
  if (const auto res = read(std::move(reader), file); !res) {
    return res.err();
  }
  return file;
}
result<void> class_reader::read(data_reader&& reader, class_file& file) {
  reader_ = std::move(reader);
  pool_.clear();
  label_count_ = 0;
  if (const auto res = read_header(); !res) {
    return res.err();
  }
  file.version = class_version_;
  const auto access_flags = reader_.read_u16();
  if (!access_flags) {
    return access_flags.err();
  }
  file.access_flags = access_flags.value();
  const auto this_class = get_string(reader_.read_u16());
  if (!this_class) {
    return this_class.err();
  }
  file.name = this_class.value();
  const auto super_name_index = reader_.read_u16();
  if (!super_name_index) {
    return super_name_index.err();
  }
  if (super_name_index.value() != 0) {
    const auto super_name = get_string(super_name_index.value());
    if (!super_name) {
      return super_name.err();
    }
    file.super_name = super_name.value();
  }
  const auto interfaces_count_res = reader_.read_u16();
  if (!interfaces_count_res) {
    return interfaces_count_res.err();
  }
  const auto interfaces_count = interfaces_count_res.value();
  file.interfaces.reserve(interfaces_count);
  for (auto i = 0; i < interfaces_count; i++) {
    const auto interface = get_string(reader_.read_u16());
    if (!interface) {
      return interface.err();
    }
    file.interfaces.emplace_back(interface.value());
  }

  const auto fields_count_res = reader_.read_u16();
  if (!fields_count_res) {
    return fields_count_res.err();
  }
  const auto fields_count = fields_count_res.value();
  const auto field_offset = reader_.cursor();
  for (auto i = 0; i < fields_count; i++) {
    if (!reader_.skip(6)) {
      return error("Failed to skip field");
    }
    const auto attributes_count_res = reader_.read_u16();
    if (!attributes_count_res) {
      return attributes_count_res.err();
    }
    const auto attributes_count = attributes_count_res.value();
    for (auto j = 0; j < attributes_count; j++) {
      if (!reader_.skip(2)) {
        return error("Failed to skip field attribute");
      }
      const auto len = reader_.read_u32();
      if (!len) {
        return len.err();
      }
      if (!reader_.skip(len.value())) {
        return error("Failed to skip field attribute data");
      }
    }
  }
  const auto methods_count_res = reader_.read_u16();
  if (!methods_count_res) {
    return methods_count_res.err();
  }
  const auto methods_count = methods_count_res.value();
  const auto method_offset = reader_.cursor();
  for (auto i = 0; i < methods_count; i++) {
    if (!reader_.skip(6)) {
      return error("Failed to skip method");
    }
    const auto attributes_count_res = reader_.read_u16();
    if (!attributes_count_res) {
      return attributes_count_res.err();
    }
    const auto attributes_count = attributes_count_res.value();
    for (auto j = 0; j < attributes_count; j++) {
      if (!reader_.skip(2)) {
        return error("Failed to skip method attribute");
      }
      const auto len = reader_.read_u32();
      if (!len) {
        return len.err();
      }
      if (!reader_.skip(len.value())) {
        return error("Failed to skip method attribute data");
      }
    }
  }
  std::vector<std::pair<size_t, label>> dummy_labels;
  const auto attributes_count_res = reader_.read_u16();
  if (!attributes_count_res) {
    return attributes_count_res.err();
  }
  const auto attributes_count = attributes_count_res.value();
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name_res = get_string(reader_.read_u16());
    if (!attribute_name_res) {
      return attribute_name_res.err();
    }
    const auto& attribute_name = attribute_name_res.value();
    const auto attribute_length_res = reader_.read_u32();
    if (!attribute_length_res) {
      return attribute_length_res.err();
    }
    const auto attribute_length = attribute_length_res.value();
    const auto attribute_start = reader_.cursor();
    const auto attribute_end = reader_.cursor() + attribute_length;

    if (attribute_name == "SourceFile") {
      const auto source_file_index = reader_.read_u16();
      if (!source_file_index) {
        goto fail;
      }
      if (source_file_index.value() != 0) {
        const auto source_file = get_string(source_file_index.value());
        if (!source_file) {
          goto fail;
        }
        file.source_file = source_file.value();
      }
    } else if (attribute_name == "InnerClasses") {
      const auto number_of_classes_res = reader_.read_u16();
      if (!number_of_classes_res) {
        goto fail;
      }
      const auto number_of_classes = number_of_classes_res.value();
      file.inner_classes.reserve(number_of_classes);
      for (auto j = 0; j < number_of_classes; j++) {
        const auto inner_name = get_string(reader_.read_u16());
        if (!inner_name) {
          goto fail;
        }
        const auto inner_outer_name_index = reader_.read_u16();
        if (!inner_outer_name_index) {
          goto fail;
        }
        std::optional<std::string> inner_outer_name = std::nullopt;
        if (inner_outer_name_index.value() != 0) {
          const auto res = get_string(inner_outer_name_index.value());
          if (!res) {
            goto fail;
          }
          inner_outer_name = res.value();
        }
        const auto inner_inner_name_index = reader_.read_u16();
        if (!inner_inner_name_index) {
          goto fail;
        }
        std::optional<std::string> inner_inner_name = std::nullopt;
        if (inner_inner_name_index.value() != 0) {
          const auto res = get_string(inner_inner_name_index.value());
          if (!res) {
            goto fail;
          }
          inner_inner_name = res.value();
        }
        const auto inner_flags = reader_.read_u16();
        if (!inner_flags) {
          goto fail;
        }
        file.inner_classes.emplace_back(inner_name.value(), inner_outer_name, inner_inner_name, inner_flags.value());
      }
    } else if (attribute_name == "EnclosingMethod") {
      const auto owner = get_string(reader_.read_u16());
      if (!owner) {
        goto fail;
      }
      const auto method_index = reader_.read_u16();
      if (!method_index) {
        goto fail;
      }
      std::optional<std::pair<std::string, std::string>> nat = std::nullopt;
      if (method_index.value() != 0) {
        const auto res = get_name_and_type(method_index.value());
        if (!res) {
          goto fail;
        }
        nat = res.value();
      }
      file.enclosing_method = std::make_pair(owner.value(), std::move(nat));
    } else if (attribute_name == "SourceDebugExtension") {
      const auto source_debug_extension = reader_.read_utf();
      if (!source_debug_extension) {
        goto fail;
      }
      file.source_debug_extension = source_debug_extension.value();
    } else if (attribute_name == "BootstrapMethods") {
      const auto num_bootstrap_methods_res = reader_.read_u16();
      if (!num_bootstrap_methods_res) {
        goto fail;
      }
      const auto num_bootstrap_methods = num_bootstrap_methods_res.value();
      bootstrap_methods_.reserve(num_bootstrap_methods);
      for (auto j = 0; j < num_bootstrap_methods; j++) {
        bootstrap_methods_.emplace_back(reader_.cursor());
        if (!reader_.skip(2)) {
          goto fail;
        }
        const auto num_bootstrap_arguments = reader_.read_u16();
        if (!num_bootstrap_arguments) {
          goto fail;
        }
        for (auto k = 0; k < num_bootstrap_arguments.value(); k++) {
          if (!reader_.skip(2)) {
            goto fail;
          }
        }
      }
    } else if (attribute_name == "Module") {
      const auto module_name = get_string(reader_.read_u16());
      if (!module_name) {
        goto fail;
      }
      const auto module_flags = reader_.read_u16();
      if (!module_flags) {
        goto fail;
      }
      const auto module_version_index = reader_.read_u16();
      std::optional<std::string> module_version = std::nullopt;
      if (module_version_index.value() != 0) {
        const auto res = get_string(module_version_index.value());
        if (!res) {
          goto fail;
        }
        module_version = res.value();
      }

      module module(module_name.value(), module_flags.value(), module_version);

      const auto requires_count_res = reader_.read_u16();
      if (!requires_count_res) {
        goto fail;
      }
      const auto requires_count = requires_count_res.value();
      module.mod_requires.reserve(requires_count);
      for (auto j = 0; j < requires_count; j++) {
        const auto requires_name = get_string(reader_.read_u16());
        if (!requires_name) {
          goto fail;
        }
        const auto requires_flags = reader_.read_u16();
        if (!requires_flags) {
          goto fail;
        }
        const auto requires_version_index = reader_.read_u16();
        if (!requires_version_index) {
          goto fail;
        }
        std::optional<std::string> requires_version = std::nullopt;
        if (requires_version_index.value() != 0) {
          const auto res = get_string(requires_version_index.value());
          if (!res) {
            goto fail;
          }
          requires_version = res.value();
        }
        module.mod_requires.emplace_back(requires_name.value(), requires_flags.value(), requires_version);
      }
      const auto exports_count_res = reader_.read_u16();
      if (!exports_count_res) {
        goto fail;
      }
      const auto exports_count = exports_count_res.value();
      module.exports.reserve(exports_count);
      for (auto j = 0; j < exports_count; j++) {
        const auto exports_name = get_string(reader_.read_u16());
        if (!exports_name) {
          goto fail;
        }
        const auto exports_flags = reader_.read_u16();
        if (!exports_flags) {
          goto fail;
        }
        const auto exports_to_count_res = reader_.read_u16();
        if (!exports_to_count_res) {
          goto fail;
        }
        const auto exports_to_count = exports_to_count_res.value();
        std::vector<std::string> exports_to;
        exports_to.reserve(exports_to_count);
        for (auto k = 0; k < exports_to_count; k++) {
          const auto exp = get_string(reader_.read_u16());
          if (!exp) {
            goto fail;
          }
          exports_to.emplace_back(exp.value());
        }
        module.exports.emplace_back(exports_name.value(), exports_flags.value(), exports_to);
      }
      const auto opens_count_res = reader_.read_u16();
      if (!opens_count_res) {
        goto fail;
      }
      const auto opens_count = opens_count_res.value();
      module.opens.reserve(opens_count);
      for (auto j = 0; j < opens_count; j++) {
        const auto opens_name = get_string(reader_.read_u16());
        if (!opens_name) {
          goto fail;
        }
        const auto opens_flags = reader_.read_u16();
        if (!opens_flags) {
          goto fail;
        }
        const auto opens_to_count_res = reader_.read_u16();
        if (!opens_to_count_res) {
          goto fail;
        }
        const auto opens_to_count = opens_to_count_res.value();
        std::vector<std::string> opens_to;
        opens_to.reserve(opens_to_count);
        for (auto k = 0; k < opens_to_count; k++) {
          const auto op = get_string(reader_.read_u16());
          if (!op) {
            goto fail;
          }
          opens_to.emplace_back(op.value());
        }
        module.opens.emplace_back(opens_name.value(), opens_flags.value(), opens_to);
      }
      const auto uses_count_res = reader_.read_u16();
      if (!uses_count_res) {
        goto fail;
      }
      const auto uses_count = uses_count_res.value();
      module.uses.reserve(uses_count);
      for (auto j = 0; j < uses_count; j++) {
        const auto uses_name = get_string(reader_.read_u16());
        if (!uses_name) {
          goto fail;
        }
        module.uses.emplace_back(uses_name.value());
      }
      const auto provides_count_res = reader_.read_u16();
      if (!provides_count_res) {
        goto fail;
      }
      const auto provides_count = provides_count_res.value();
      module.provides.reserve(provides_count);
      for (auto j = 0; j < provides_count; j++) {
        const auto provides_service = get_string(reader_.read_u16());
        if (!provides_service) {
          goto fail;
        }
        const auto provides_with_count_res = reader_.read_u16();
        if (!provides_with_count_res) {
          goto fail;
        }
        const auto provides_with_count = provides_with_count_res.value();
        std::vector<std::string> provides_with;
        provides_with.reserve(provides_with_count);
        for (auto k = 0; k < provides_with_count; k++) {
          const auto p = get_string(reader_.read_u16());
          if (!p) {
            goto fail;
          }
          provides_with.emplace_back(p.value());
        }
        module.provides.emplace_back(provides_service.value(), std::move(provides_with));
      }
      file.mod = std::move(module);
    } else if (attribute_name == "ModulePackages") {
      const auto package_count_res = reader_.read_u16();
      if (!package_count_res) {
        goto fail;
      }
      const auto package_count = package_count_res.value();
      file.module_packages.reserve(package_count);
      for (auto j = 0; j < package_count; j++) {
        const auto package = get_string(reader_.read_u16());
        if (!package) {
          goto fail;
        }
        file.module_packages.emplace_back(package.value());
      }
    } else if (attribute_name == "ModuleMainClass") {
      const auto main_class_index = reader_.read_u16();
      if (!main_class_index) {
        goto fail;
      }
      if (main_class_index.value() != 0) {
        const auto main_class = get_string(main_class_index.value());
        if (!main_class) {
          goto fail;
        }
        file.module_main_class = main_class.value();
      }
    } else if (attribute_name == "NestHost") {
      const auto nest_host_index = reader_.read_u16();
      if (!nest_host_index) {
        goto fail;
      }
      if (nest_host_index.value() != 0) {
        const auto nest_host = get_string(nest_host_index.value());
        if (!nest_host) {
          goto fail;
        }
        file.nest_host = nest_host.value();
      }
    } else if (attribute_name == "NestMembers") {
      const auto member_count_res = reader_.read_u16();
      if (!member_count_res) {
        goto fail;
      }
      const auto member_count = member_count_res.value();
      file.nest_members.reserve(member_count);
      for (auto j = 0; j < member_count; j++) {
        const auto member = get_string(reader_.read_u16());
        if (!member) {
          goto fail;
        }
        file.nest_members.emplace_back(member.value());
      }
    } else if (attribute_name == "Record") {
      if (!read_record(file)) {
        goto fail;
      }
    } else if (attribute_name == "PermittedSubclasses") {
      const auto number_of_classes_res = reader_.read_u16();
      if (!number_of_classes_res) {
        goto fail;
      }
      const auto number_of_classes = number_of_classes_res.value();
      file.permitted_subclasses.reserve(number_of_classes);
      for (auto j = 0; j < number_of_classes; j++) {
        const auto subclass = get_string(reader_.read_u16());
        if (!subclass) {
          goto fail;
        }
        file.permitted_subclasses.emplace_back(subclass.value());
      }
    } else if (attribute_name == "Synthetic") {
      file.synthetic = true;
    } else if (attribute_name == "Deprecated") {
      file.deprecated = true;
    } else if (attribute_name == "Signature") {
      const auto signature_index = reader_.read_u16();
      if (!signature_index) {
        goto fail;
      }
      if (signature_index.value() != 0) {
        const auto signature = get_string(signature_index.value());
        if (!signature) {
          goto fail;
        }
        file.signature = signature.value();
      }
    } else if (attribute_name == "RuntimeVisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      file.visible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        file.visible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      file.invisible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        file.invisible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      file.visible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        file.visible_type_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      file.invisible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        file.invisible_type_annotations.emplace_back(anno.value());
      }
    } else {
    fail:
      auto data = reader_.bytes(attribute_start, attribute_end);
      if (!data) {
        return data.err();
      }
      file.attributes.emplace_back(attribute_name, data.value());
    }
    if (!reader_.cursor(attribute_end)) {
      return error("Failed to read class");
    }
  }

  if (!reader_.cursor(field_offset)) {
    return error("Failed to read class");
  }
  file.fields.reserve(fields_count);
  for (auto i = 0; i < fields_count; i++) {
    if (const auto field = read_field(file); !field) {
      return field.err();
    }
  }

  if (!reader_.cursor(method_offset)) {
    return error("Failed to read class");
  }
  file.methods.reserve(methods_count);
  for (auto i = 0; i < methods_count; i++) {
    if (const auto method = read_method(file); !method) {
      return method.err();
    }
  }
  return {};
}
result<void> class_reader::read_code(code& code, uint32_t code_length) {
  std::vector<std::pair<size_t, label>> labels;
  const auto bytecode_start = reader_.cursor();
  const auto bytecode_end = bytecode_start + code_length;
  uint8_t wide = 0;
  while (reader_.cursor() < bytecode_end) {
    const auto insn_start = reader_.cursor() - bytecode_start;
    const auto op_res = reader_.read_u8();
    if (!op_res) {
      return op_res.err();
    }
    switch (op_res.value()) {
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
      case op::ret:
        if (wide > 0) {
          if (!reader_.skip(2)) {
            return error("Failed to read code");
          }
        } else {
          if (!reader_.skip(1)) {
            return error("Failed to read code");
          }
        }
        break;
      case op::anewarray:
      case op::checkcast:
      case op::getfield:
      case op::getstatic:
      case op::iinc:
      case op::instanceof:
      case op::invokespecial:
      case op::invokestatic:
      case op::invokevirtual:
      case op::ldc_w:
      case op::ldc2_w:
      case op::new_:
      case op::putfield:
      case op::putstatic:
      case op::sipush:
        if (!reader_.skip(2)) {
          return error("Failed to read code");
        }
        break;
      case op::bipush:
      case op::ldc:
      case op::newarray:
        if (!reader_.skip(1)) {
          return error("Failed to read code");
        }
        break;
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
        const auto offset = reader_.read_i16();
        if (!offset) {
          return offset.err();
        }
        get_label(labels, insn_start + offset.value());
        break;
      }
      case op::goto_w:
      case op::jsr_w: {
        const auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        get_label(labels, insn_start + offset.value());
        break;
      }
      case op::invokedynamic:
      case op::invokeinterface:
        if (!reader_.skip(4)) {
          return error("Failed to read code");
        }
        break;
      case op::lookupswitch: {
        const auto pc = reader_.cursor() - bytecode_start;
        if (!reader_.skip((4 - (pc % 4)) % 4)) {
          return error("Failed to read code");
        }
        auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        get_label(labels, insn_start + offset.value());
        const auto npairs_res = reader_.read_i32();
        if (!npairs_res) {
          return npairs_res.err();
        }
        const auto npairs = npairs_res.value();
        for (auto i = 0; i < npairs; i++) {
          if (!reader_.skip(4)) {
            return error("Failed to read code");
          }
          offset = reader_.read_i32();
          if (!offset) {
            return offset.err();
          }
          get_label(labels, insn_start + offset.value());
        }
        break;
      }
      case op::multianewarray: {
        if (!reader_.skip(3)) {
          return error("Failed to read code");
        }
        break;
      }
      case op::tableswitch: {
        const auto pc = reader_.cursor() - bytecode_start;
        if (!reader_.skip((4 - (pc % 4)) % 4)) {
          return error("Failed to read code");
        }
        auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        get_label(labels, insn_start + offset.value());
        const auto low = reader_.read_i32();
        if (!low) {
          return low.err();
        }
        const auto high = reader_.read_i32();
        if (!high) {
          return high.err();
        }
        for (auto i = low.value(); i <= high.value(); i++) {
          offset = reader_.read_i32();
          if (!offset) {
            return offset.err();
          }
          get_label(labels, insn_start + offset.value());
        }
        break;
      }
      case op::wide:
        wide = 2;
        break;
      default: {
        // do nothing, single byte instruction
      }
    }
    if (wide > 0) {
      wide--;
    }
  }
  const auto exception_table_length_res = reader_.read_u16();
  if (!exception_table_length_res) {
    return exception_table_length_res.err();
  }
  const auto exception_table_length = exception_table_length_res.value();
  code.tcbs.reserve(exception_table_length);
  for (auto i = 0; i < exception_table_length; i++) {
    auto offset = reader_.read_u16();
    if (!offset) {
      return offset.err();
    }
    const auto start = get_label(labels, offset.value());
    offset = reader_.read_u16();
    if (!offset) {
      return offset.err();
    }
    const auto end = get_label(labels, offset.value());
    offset = reader_.read_u16();
    if (!offset) {
      return offset.err();
    }
    const auto handler = get_label(labels, offset.value());
    const auto catch_type_index = reader_.read_u16();
    if (!catch_type_index) {
      return catch_type_index.err();
    }
    std::optional<std::string> catch_type = std::nullopt;
    if (catch_type_index.value() != 0) {
      auto type_res = get_string(catch_type_index.value());
      if (!type_res) {
        return type_res.err();
      }
      catch_type = type_res.value();
    }
    code.tcbs.emplace_back(start, end, handler, catch_type);
  }
  const auto attributes_count_res = reader_.read_u16();
  if (!attributes_count_res) {
    return attributes_count_res.err();
  }
  const auto attributes_count = attributes_count_res.value();
  bool should_search_local = false;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name_res = get_string(reader_.read_u16());
    if (!attribute_name_res) {
      return attribute_name_res.err();
    }
    const auto attribute_length_res = reader_.read_u32();
    if (!attribute_length_res) {
      return attribute_length_res.err();
    }
    const auto attribute_length = attribute_length_res.value();
    const auto& attribute_name = attribute_name_res.value();
    const auto attribute_start = reader_.cursor();
    const auto attribute_end = reader_.cursor() + attribute_length;
    if (attribute_name == "LineNumberTable") {
      const auto line_number_table_length_res = reader_.read_u16();
      if (!line_number_table_length_res) {
        goto fail;
      }
      const auto line_number_table_length = line_number_table_length_res.value();
      code.line_numbers.reserve(line_number_table_length);
      for (auto j = 0; j < line_number_table_length; j++) {
        const auto start_pc_res = reader_.read_u16();
        if (!start_pc_res) {
          goto fail;
        }
        const auto start_pc = start_pc_res.value();
        const auto line_number_res = reader_.read_u16();
        if (!line_number_res) {
          goto fail;
        }
        const auto line_number = line_number_res.value();
        const auto start_label = get_label(labels, start_pc);
        code.line_numbers.emplace_back(line_number, start_label);
      }
    } else if (attribute_name == "LocalVariableTable") {
      const auto local_variable_table_length_res = reader_.read_u16();
      if (!local_variable_table_length_res) {
        goto fail;
      }
      const auto local_variable_table_length = local_variable_table_length_res.value();
      if (!should_search_local) {
        code.locals.reserve(local_variable_table_length);
      }
      for (auto j = 0; j < local_variable_table_length; j++) {
        const auto start_pc_res = reader_.read_u16();
        if (!start_pc_res) {
          goto fail;
        }
        const auto start_pc = start_pc_res.value();
        const auto length_res = reader_.read_u16();
        if (!length_res) {
          goto fail;
        }
        const auto length = length_res.value();
        const auto start = get_label(labels, start_pc);
        const auto end = get_label(labels, start_pc + length);
        const auto name = get_string(reader_.read_u16());
        if (!name) {
          goto fail;
        }
        const auto desc = get_string(reader_.read_u16());
        if (!desc) {
          goto fail;
        }
        const auto index_res = reader_.read_u16();
        if (!index_res) {
          goto fail;
        }
        const auto index = index_res.value();
        if (should_search_local) {
          bool found = false;
          for (auto& local : code.locals) {
            if (local.start == start && local.index == index) {
              local.desc = desc.value();
              found = true;
              break;
            }
          }
          if (found) {
            continue;
          }
        }
        code.locals.emplace_back(name.value(), desc.value(), "", start, end, index);
      }
      should_search_local = true;
    } else if (attribute_name == "LocalVariableTypeTable") {
      const auto local_variable_type_table_length_res = reader_.read_u16();
      if (!local_variable_type_table_length_res) {
        goto fail;
      }
      const auto local_variable_type_table_length = local_variable_type_table_length_res.value();
      if (!should_search_local) {
        code.locals.reserve(local_variable_type_table_length);
      }
      for (auto j = 0; j < local_variable_type_table_length; j++) {
        const auto start_pc_res = reader_.read_u16();
        if (!start_pc_res) {
          goto fail;
        }
        const auto start_pc = start_pc_res.value();
        const auto length_res = reader_.read_u16();
        if (!length_res) {
          goto fail;
        }
        const auto length = length_res.value();
        const auto start = get_label(labels, start_pc);
        const auto end = get_label(labels, start_pc + length);
        const auto name = get_string(reader_.read_u16());
        if (!name) {
          goto fail;
        }
        const auto signature = get_string(reader_.read_u16());
        if (!signature) {
          goto fail;
        }
        const auto index_res = reader_.read_u16();
        if (!index_res) {
          goto fail;
        }
        const auto index = index_res.value();
        if (should_search_local) {
          bool found = false;
          for (auto& local : code.locals) {
            if (local.start == start && local.index == index) {
              local.signature = signature.value();
              found = true;
              break;
            }
          }
          if (found) {
            continue;
          }
        }
        code.locals.emplace_back(name.value(), "", signature.value(), start, end, index);
      }
    } else if (attribute_name == "StackMapTable") {
      const auto number_of_entries_res = reader_.read_u16();
      if (!number_of_entries_res) {
        goto fail;
      }
      const auto number_of_entries = number_of_entries_res.value();
      uint32_t previous_offset = 0;
      code.frames.reserve(number_of_entries);
      for (auto j = 0; j < number_of_entries; j++) {
        const auto frame_type_res = reader_.read_u8();
        if (!frame_type_res) {
          goto fail;
        }
        const auto frame_type = frame_type_res.value();
        const auto first = j == 0;
        if (frame_type <= 63) {
          const auto delta = frame_type;
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto target = get_label(labels, offset_delta);
          code.frames.emplace_back(target, same_frame());
        } else if (frame_type <= 127) {
          const auto delta = frame_type - 64;
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto target = get_label(labels, offset_delta);
          const auto var = read_frame_var(labels);
          if (!var) {
            goto fail;
          }
          code.frames.emplace_back(target, same_frame(var.value()));
        } else if (frame_type == 247) {
          const auto delta_res = reader_.read_u16();
          if (!delta_res) {
            goto fail;
          }
          const auto delta = delta_res.value();
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto target = get_label(labels, offset_delta);
          const auto var = read_frame_var(labels);
          if (!var) {
            goto fail;
          }
          code.frames.emplace_back(target, same_frame(var.value()));
        } else if (frame_type >= 248 && frame_type <= 250) {
          const auto delta_res = reader_.read_u16();
          if (!delta_res) {
            goto fail;
          }
          const auto delta = delta_res.value();
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto target = get_label(labels, offset_delta);
          code.frames.emplace_back(target, chop_frame(251 - frame_type));
        } else if (frame_type == 251) {
          const auto delta_res = reader_.read_u16();
          if (!delta_res) {
            goto fail;
          }
          const auto delta = delta_res.value();
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto target = get_label(labels, offset_delta);
          code.frames.emplace_back(target, same_frame());
        } else if (frame_type >= 252 && frame_type <= 254) {
          const auto delta_res = reader_.read_u16();
          if (!delta_res) {
            goto fail;
          }
          const auto delta = delta_res.value();
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          std::vector<frame_var> locals;
          const auto locals_count = frame_type - 251;
          locals.reserve(locals_count);
          for (auto k = 0; k < locals_count; k++) {
            const auto var = read_frame_var(labels);
            if (!var) {
              goto fail;
            }
            locals.emplace_back(var.value());
          }
          const auto target = get_label(labels, offset_delta);
          code.frames.emplace_back(target, append_frame(locals));
        } else if (frame_type == 255) {
          const auto delta_res = reader_.read_u16();
          if (!delta_res) {
            goto fail;
          }
          const auto delta = delta_res.value();
          const auto offset_delta = first ? delta : previous_offset + delta + 1;
          previous_offset = offset_delta;
          const auto number_of_locals_res = reader_.read_u16();
          if (!number_of_locals_res) {
            goto fail;
          }
          const auto number_of_locals = number_of_locals_res.value();
          std::vector<frame_var> locals;
          locals.reserve(number_of_locals);
          for (auto k = 0; k < number_of_locals; k++) {
            const auto var = read_frame_var(labels);
            if (!var) {
              goto fail;
            }
            locals.emplace_back(var.value());
          }
          const auto number_of_stack_items_res = reader_.read_u16();
          if (!number_of_stack_items_res) {
            goto fail;
          }
          const auto number_of_stack_items = number_of_stack_items_res.value();
          std::vector<frame_var> stack;
          stack.reserve(number_of_stack_items);
          for (auto k = 0; k < number_of_stack_items; k++) {
            const auto var = read_frame_var(labels);
            if (!var) {
              goto fail;
            }
            stack.emplace_back(var.value());
          }
          const auto target = get_label(labels, offset_delta);
          code.frames.emplace_back(target, full_frame(locals, stack));
        }
      }
    } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      code.visible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(labels);
        if (!anno) {
          goto fail;
        }
        code.visible_type_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      code.invisible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(labels);
        if (!anno) {
          goto fail;
        }
        code.visible_type_annotations.emplace_back(anno.value());
      }
    } else {
    fail:
      auto data = reader_.bytes(attribute_start, attribute_end);
      if (!data) {
        return data.err();
      }
      code.attributes.emplace_back(attribute_name, data.value());
    }

    if (!reader_.cursor(attribute_end)) {
      return error("Failed to read code");
    }
  }
  const auto code_end = reader_.cursor();
  if (!reader_.cursor(bytecode_start)) {
    return error("Failed to read code");
  }
  wide = 0;

  while (reader_.cursor() < bytecode_end) {
    const auto insn_start = reader_.cursor() - bytecode_start;
    if (auto lbl = get_label_opt(labels, insn_start)) {
      code.emplace_back(*lbl);
    }
    const auto opcode_res = reader_.read_u8();
    if (!opcode_res) {
      return opcode_res.err();
    }
    switch (const auto opcode = opcode_res.value()) {
      case op::aload_0:
      case op::aload_1:
      case op::aload_2:
      case op::aload_3:
        code.add_var_insn(op::aload, opcode - op::aload_0);
        break;
      case op::astore_0:
      case op::astore_1:
      case op::astore_2:
      case op::astore_3:
        code.add_var_insn(op::astore, opcode - op::astore_0);
        break;
      case op::dload_0:
      case op::dload_1:
      case op::dload_2:
      case op::dload_3:
        code.add_var_insn(op::dload, opcode - op::dload_0);
        break;
      case op::dstore_0:
      case op::dstore_1:
      case op::dstore_2:
      case op::dstore_3:
        code.add_var_insn(op::dstore, opcode - op::dstore_0);
        break;
      case op::fstore_0:
      case op::fstore_1:
      case op::fstore_2:
      case op::fstore_3:
        code.add_var_insn(op::fstore, opcode - op::fstore_0);
        break;
      case op::fload_0:
      case op::fload_1:
      case op::fload_2:
      case op::fload_3:
        code.add_var_insn(op::fload, opcode - op::fload_0);
        break;
      case op::iload_0:
      case op::iload_1:
      case op::iload_2:
      case op::iload_3:
        code.add_var_insn(op::iload, opcode - op::iload_0);
        break;
      case op::istore_0:
      case op::istore_1:
      case op::istore_2:
      case op::istore_3:
        code.add_var_insn(op::istore, opcode - op::istore_0);
        break;
      case op::lload_0:
      case op::lload_1:
      case op::lload_2:
      case op::lload_3:
        code.add_var_insn(op::lload, opcode - op::lload_0);
        break;
      case op::lstore_0:
      case op::lstore_1:
      case op::lstore_2:
      case op::lstore_3:
        code.add_var_insn(op::lstore, opcode - op::lstore_0);
        break;
      case op::iconst_m1:
      case op::iconst_0:
      case op::iconst_1:
      case op::iconst_2:
      case op::iconst_3:
      case op::iconst_4:
      case op::iconst_5:
        code.add_push_insn(static_cast<int32_t>(opcode - op::iconst_0));
        break;
      case op::fconst_0:
      case op::fconst_1:
      case op::fconst_2:
        code.add_push_insn(static_cast<float>(opcode - op::fconst_0));
        break;
      case op::dconst_0:
      case op::dconst_1:
        code.add_push_insn(static_cast<double>(opcode - op::dconst_0));
        break;
      case op::lconst_0:
      case op::lconst_1:
        code.add_push_insn(static_cast<int64_t>(opcode - op::lconst_0));
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
        code.add_insn(opcode);
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
        uint16_t index;
        if (wide > 0) {
          const auto index_res = reader_.read_u16();
          if (!index_res) {
            return index_res.err();
          }
          index = index_res.value();
        } else {
          const auto index_res = reader_.read_u8();
          if (!index_res) {
            return index_res.err();
          }
          index = index_res.value();
        }
        code.add_var_insn(opcode, index);
        break;
      }
      case op::anewarray: {
        const auto desc = get_string(reader_.read_u16());
        if (!desc) {
          return desc.err();
        }
        code.add_array_insn(desc.value());
        break;
      }
      case op::bipush: {
        const auto value = reader_.read_i8();
        if (!value) {
          return value.err();
        }
        code.add_push_insn(value.value());
        break;
      }
      case op::sipush: {
        const auto value = reader_.read_i16();
        if (!value) {
          return value.err();
        }
        code.add_push_insn(value.value());
        break;
      }
      case op::checkcast:
      case op::instanceof:
      case op::new_: {
        const auto desc = get_string(reader_.read_u16());
        if (!desc) {
          return desc.err();
        }
        code.add_type_insn(opcode, desc.value());
        break;
      }
      case op::getfield:
      case op::getstatic:
      case op::putfield:
      case op::putstatic: {
        const auto ref = get_ref(reader_.read_u16());
        if (!ref) {
          return ref.err();
        }
        const auto [owner, name, desc, interface] = ref.value();
        code.add_field_insn(opcode, owner, name, desc);
        break;
      }
      case op::invokespecial:
      case op::invokestatic:
      case op::invokevirtual: {
        const auto ref = get_ref(reader_.read_u16());
        if (!ref) {
          return ref.err();
        }
        const auto [owner, name, desc, interface] = ref.value();
        code.add_method_insn(opcode, owner, name, desc, interface);
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
        const auto offset = reader_.read_i16();
        if (!offset) {
          return offset.err();
        }
        const auto target = get_label(labels, insn_start + offset.value());
        code.add_branch_insn(opcode, target);
        break;
      }
      case op::goto_w:
      case op::jsr_w: {
        const auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        const auto target = get_label(labels, insn_start + offset.value());
        code.add_branch_insn(opcode, target);
        break;
      }
      case op::iinc: {
        uint16_t index;
        if (wide > 0) {
          const auto index_res = reader_.read_u16();
          if (!index_res) {
            return index_res.err();
          }
          index = index_res.value();
        } else {
          const auto index_res = reader_.read_u8();
          if (!index_res) {
            return index_res.err();
          }
          index = index_res.value();
        }
        int16_t value;
        if (wide > 0) {
          const auto value_res = reader_.read_i16();
          if (!value_res) {
            return value_res.err();
          }
          value = value_res.value();
        } else {
          const auto value_res = reader_.read_i8();
          if (!value_res) {
            return value_res.err();
          }
          value = static_cast<int16_t>(value_res.value());
        }
        code.add_iinc_insn(index, value);
        break;
      }
      case op::invokedynamic: {
        const auto cp_index = reader_.read_u16();
        if (!cp_index) {
          return cp_index.err();
        }
        if (const auto dyn = std::get_if<cp::invoke_dynamic_info>(&pool_[cp_index.value()])) {
          if (!reader_.skip(2)) {
            return error("Invalid invoke dynamic instruction");
          }
          const auto curr = reader_.cursor();
          if (!reader_.cursor(bootstrap_methods_[dyn->bootstrap_method_attr_index])) {
            return error("Invalid invoke dynamic instruction");
          }
          const auto handle = get_method_handle(reader_.read_u16());
          if (!handle) {
            return handle.err();
          }
          const auto arg_count_res = reader_.read_u16();
          if (!arg_count_res) {
            return arg_count_res.err();
          }
          const auto arg_count = arg_count_res.value();
          std::vector<uint16_t> arg_indices;
          arg_indices.reserve(arg_count);
          for (auto i = 0; i < arg_count; i++) {
            const auto arg = reader_.read_u16();
            if (!arg) {
              return arg.err();
            }
            arg_indices.emplace_back(arg.value());
          }
          if (!reader_.cursor(curr)) {
            return error("Invalid invoke dynamic instruction");
          }
          std::vector<value> args;
          args.reserve(arg_count);
          for (const auto arg_index : arg_indices) {
            const auto arg = get_constant(arg_index);
            if (!arg) {
              return arg.err();
            }
            args.emplace_back(arg.value());
          }
          const auto nat = get_name_and_type(dyn->name_and_type_index);
          if (!nat) {
            return nat.err();
          }
          const auto [name, desc] = nat.value();
          code.add_invoke_dynamic_insn(name, desc, handle.value(), args);
          break;
        }
        return error("Invalid invoke dynamic instruction");
      }
      case op::invokeinterface: {
        const auto ref = get_ref(reader_.read_u16());
        if (!ref) {
          return ref.err();
        }
        const auto [owner, name, desc, interface] = ref.value();
        if (!reader_.skip(2)) {
          return error("Invalid invoke interface instruction");
        }
        code.add_method_insn(opcode, owner, name, desc, interface);
        break;
      }
      case op::ldc: {
        const auto value = get_constant(reader_.read_u8().map([](uint8_t v) { return static_cast<uint16_t>(v); }));
        if (!value) {
          return value.err();
        }
        code.add_push_insn(value.value());
        break;
      }
      case op::ldc_w:
      case op::ldc2_w: {
        const auto value = get_constant(reader_.read_u16());
        if (!value) {
          return value.err();
        }
        code.add_push_insn(value.value());
        break;
      }
      case op::lookupswitch: {
        const auto pc = reader_.cursor() - bytecode_start;
        if (!reader_.skip((4 - (pc % 4)) % 4)) {
          return error("Invalid lookup switch instruction");
        }
        auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        const auto default_target = get_label(labels, insn_start + offset.value());
        std::vector<std::pair<int32_t, label>> pairs;
        const auto npairs_res = reader_.read_i32();
        if (!npairs_res) {
          return npairs_res.err();
        }
        const auto npairs = npairs_res.value();
        pairs.reserve(npairs);
        for (auto i = 0; i < npairs; i++) {
          const auto match = reader_.read_i32();
          if (!match) {
            return match.err();
          }
          offset = reader_.read_i32();
          if (!offset) {
            return offset.err();
          }
          const auto target = get_label(labels, insn_start + offset.value());
          pairs.emplace_back(match.value(), target);
        }
        code.add_lookup_switch_insn(default_target, pairs);
        break;
      }
      case op::multianewarray: {
        const auto desc = get_string(reader_.read_u16());
        if (!desc) {
          return desc.err();
        }
        const auto dimensions = reader_.read_u8();
        if (!dimensions) {
          return dimensions.err();
        }
        code.add_multi_array_insn(desc.value(), dimensions.value());
        break;
      }
      case op::newarray: {
        const auto ty = reader_.read_u8();
        if (!ty) {
          return ty.err();
        }
        code.add_array_insn(ty.value());
        break;
      }
      case op::tableswitch: {
        const auto pc = reader_.cursor() - bytecode_start;
        if (!reader_.skip((4 - (pc % 4)) % 4)) {
          return error("Invalid table switch instruction");
        }
        auto offset = reader_.read_i32();
        if (!offset) {
          return offset.err();
        }
        const auto default_target = get_label(labels, insn_start + offset.value());
        const auto low_res = reader_.read_i32();
        if (!low_res) {
          return low_res.err();
        }
        const auto high_res = reader_.read_i32();
        if (!high_res) {
          return high_res.err();
        }
        const auto low = low_res.value();
        const auto high = high_res.value();
        std::vector<label> targets;
        targets.reserve(high - low + 1);
        for (auto i = low; i <= high; i++) {
          offset = reader_.read_i32();
          if (!offset) {
            return offset.err();
          }
          targets.emplace_back(get_label(labels, insn_start + offset.value()));
        }
        code.add_table_switch_insn(default_target, low, high, targets);
        break;
      }
      case op::wide:
        wide = 2;
        break;
      default:
        return error("Unknown opcode: " + std::to_string(opcode));
    }
    if (wide > 0) {
      wide--;
    }
  }
  if (const auto lbl = get_label_opt(labels, code_length)) {
    code.add_label(*lbl);
  }
  if (!reader_.cursor(code_end)) {
    return error("Failed to read code");
  }
  return {};
}
result<void> class_reader::read_method(class_file& file) {
  label_count_ = 0;
  const auto access_flags = reader_.read_u16();
  if (!access_flags) {
    return access_flags.err();
  }
  const auto name = get_string(reader_.read_u16());
  if (!name) {
    return name.err();
  }
  const auto desc = get_string(reader_.read_u16());
  if (!desc) {
    return desc.err();
  }
  method method(access_flags.value(), name.value(), desc.value());
  const auto attributes_count_res = reader_.read_u16();
  if (!attributes_count_res) {
    return attributes_count_res.err();
  }
  const auto attributes_count = attributes_count_res.value();
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name_res = get_string(reader_.read_u16());
    if (!attribute_name_res) {
      return attribute_name_res.err();
    }
    const auto& attribute_name = attribute_name_res.value();
    const auto attribute_length_res = reader_.read_u32();
    if (!attribute_length_res) {
      return attribute_length_res.err();
    }
    const auto attribute_length = attribute_length_res.value();
    const auto attribute_start = reader_.cursor();
    const auto attribute_end = reader_.cursor() + attribute_length;
    if (attribute_name == "Code") {
      const auto oak = class_version_ < class_version::v1_1;
      uint16_t max_stack;
      uint16_t max_locals;
      uint32_t code_length;
      if (oak) {
        auto max_stack_res = reader_.read_u8();
        if (!max_stack_res) {
          goto fail;
        }
        auto max_locals_res = reader_.read_u8();
        if (!max_locals_res) {
          goto fail;
        }
        auto code_length_res = reader_.read_u16();
        if (!code_length_res) {
          goto fail;
        }
        max_stack = max_stack_res.value();
        max_locals = max_locals_res.value();
        code_length = code_length_res.value();
      } else {
        auto max_stack_res = reader_.read_u16();
        if (!max_stack_res) {
          goto fail;
        }
        auto max_locals_res = reader_.read_u16();
        if (!max_locals_res) {
          goto fail;
        }
        auto code_length_res = reader_.read_u32();
        if (!code_length_res) {
          goto fail;
        }
        max_stack = max_stack_res.value();
        max_locals = max_locals_res.value();
        code_length = code_length_res.value();
      }
      method.body = code(max_stack, max_locals);
      auto res = read_code(method.body, code_length);
      if (!res) {
        goto fail;
      }
    } else if (attribute_name == "Exceptions") {
      const auto number_of_exceptions_res = reader_.read_u16();
      if (!number_of_exceptions_res) {
        goto fail;
      }
      const auto number_of_exceptions = number_of_exceptions_res.value();
      method.exceptions.reserve(number_of_exceptions);
      for (auto j = 0; j < number_of_exceptions; j++) {
        const auto ex = get_string(reader_.read_u16());
        if (!ex) {
          goto fail;
        }
        method.exceptions.emplace_back(ex.value());
      }
    } else if (attribute_name == "RuntimeVisibleParameterAnnotations") {
      const auto num_parameters_res = reader_.read_u8();
      if (!num_parameters_res) {
        goto fail;
      }
      const auto num_parameters = num_parameters_res.value();
      method.visible_parameter_annotations.resize(num_parameters);
      for (auto j = 0; j < num_parameters; j++) {
        auto& params = method.visible_parameter_annotations[j];
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        params.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_annotation();
          if (!anno) {
            goto fail;
          }
          params.emplace_back(anno.value());
        }
      }
    } else if (attribute_name == "RuntimeInvisibleParameterAnnotations") {
      const auto num_parameters_res = reader_.read_u8();
      if (!num_parameters_res) {
        goto fail;
      }
      const auto num_parameters = num_parameters_res.value();
      method.invisible_parameter_annotations.resize(num_parameters);
      for (auto j = 0; j < num_parameters; j++) {
        auto& params = method.invisible_parameter_annotations[j];
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        params.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_annotation();
          if (!anno) {
            goto fail;
          }
          params.emplace_back(anno.value());
        }
      }
    } else if (attribute_name == "AnnotationDefault") {
      const auto anno = read_element_value();
      if (!anno) {
        goto fail;
      }
      method.annotation_default = anno.value();
    } else if (attribute_name == "MethodParameters") {
      const auto parameters_count_res = reader_.read_u8();
      if (!parameters_count_res) {
        goto fail;
      }
      const auto parameters_count = parameters_count_res.value();
      method.parameters.reserve(parameters_count);
      for (auto j = 0; j < parameters_count; j++) {
        const auto name_index = reader_.read_u16();
        if (!name_index) {
          goto fail;
        }
        std::optional<std::string> param_name;
        if (name_index.value() != 0) {
          const auto pname = get_string(name_index.value());
          if (!pname) {
            goto fail;
          }
          param_name = pname.value();
        }
        const auto param_access_flags = reader_.read_u16();
        if (!param_access_flags) {
          goto fail;
        }
        method.parameters.emplace_back(param_access_flags.value(), param_name);
      }
    } else if (attribute_name == "Synthetic") {
      method.synthetic = true;
    } else if (attribute_name == "Deprecated") {
      method.deprecated = true;
    } else if (attribute_name == "Signature") {
      const auto sig_index_res = reader_.read_u16();
      if (!sig_index_res) {
        goto fail;
      }
      const auto sig_index = sig_index_res.value();
      if (sig_index != 0) {
        const auto sig = get_string(sig_index);
        if (!sig) {
          goto fail;
        }
        method.signature = sig.value();
      }
    } else if (attribute_name == "RuntimeVisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      method.visible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        method.visible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      method.invisible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        method.invisible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      method.visible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        method.visible_type_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      method.invisible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        method.invisible_type_annotations.emplace_back(anno.value());
      }
    } else {
    fail:
      auto data = reader_.bytes(attribute_start, attribute_end);
      if (!data) {
        return data.err();
      }
      method.attributes.emplace_back(attribute_name, data.value());
    }
    if (!reader_.cursor(attribute_end)) {
      return error("Failed to read method attribute");
    }
  }
  file.methods.emplace_back(std::move(method));
  return {};
}
result<void> class_reader::read_field(class_file& file) {
  const auto access_flags = reader_.read_u16();
  if (!access_flags) {
    return access_flags.err();
  }
  const auto name = get_string(reader_.read_u16());
  if (!name) {
    return name.err();
  }
  const auto desc = get_string(reader_.read_u16());
  if (!desc) {
    return desc.err();
  }
  field field(access_flags.value(), name.value(), desc.value());
  const auto attributes_count_res = reader_.read_u16();
  if (!attributes_count_res) {
    return attributes_count_res.err();
  }
  const auto attributes_count = attributes_count_res.value();
  std::vector<std::pair<size_t, label>> dummy_labels;
  for (auto i = 0; i < attributes_count; i++) {
    const auto attribute_name_res = get_string(reader_.read_u16());
    if (!attribute_name_res) {
      return attribute_name_res.err();
    }
    const auto& attribute_name = attribute_name_res.value();
    const auto attribute_length_res = reader_.read_u32();
    if (!attribute_length_res) {
      return attribute_length_res.err();
    }
    const auto attribute_length = attribute_length_res.value();
    const auto attribute_start = reader_.cursor();
    const auto attribute_end = reader_.cursor() + attribute_length;
    if (attribute_name == "ConstantValue") {
      const auto val = get_constant(reader_.read_u16());
      if (!val) {
        goto fail;
      }
      field.constant_value = val.value();
    } else if (attribute_name == "Synthetic") {
      field.synthetic = true;
    } else if (attribute_name == "Deprecated") {
      field.deprecated = true;
    } else if (attribute_name == "Signature") {
      const auto sig_index_res = reader_.read_u16();
      if (!sig_index_res) {
        goto fail;
      }
      const auto sig_index = sig_index_res.value();
      if (sig_index != 0) {
        const auto sig = get_string(sig_index);
        if (!sig) {
          goto fail;
        }
        field.signature = sig.value();
      }
    } else if (attribute_name == "RuntimeVisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      field.visible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        field.visible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      field.invisible_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_annotation();
        if (!anno) {
          goto fail;
        }
        field.invisible_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      field.visible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        field.visible_type_annotations.emplace_back(anno.value());
      }
    } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
      const auto num_annotations_res = reader_.read_u16();
      if (!num_annotations_res) {
        goto fail;
      }
      const auto num_annotations = num_annotations_res.value();
      field.invisible_type_annotations.reserve(num_annotations);
      for (auto j = 0; j < num_annotations; j++) {
        const auto anno = read_type_annotation(dummy_labels);
        if (!anno) {
          goto fail;
        }
        field.invisible_type_annotations.emplace_back(anno.value());
      }
    } else {
    fail:
      auto data = reader_.bytes(attribute_start, attribute_end);
      if (!data) {
        return data.err();
      }
      field.attributes.emplace_back(attribute_name, data.value());
    }
    if (!reader_.cursor(attribute_end)) {
      return error("Failed to read field attribute");
    }
  }
  file.fields.emplace_back(std::move(field));
  return {};
}
result<void> class_reader::read_record(class_file& file) {
  const auto component_count_res = reader_.read_u16();
  if (!component_count_res) {
    return component_count_res.err();
  }
  const auto component_count = component_count_res.value();
  std::vector<std::pair<size_t, label>> dummy_labels;
  file.record_components.reserve(component_count);
  for (auto i = 0; i < component_count; i++) {
    const auto name = get_string(reader_.read_u16());
    if (!name) {
      return name.err();
    }
    const auto desc = get_string(reader_.read_u16());
    if (!desc) {
      return desc.err();
    }
    record_component component(name.value(), desc.value());
    const auto attribute_count_res = reader_.read_u16();
    if (!attribute_count_res) {
      return attribute_count_res.err();
    }
    const auto attribute_count = attribute_count_res.value();
    for (auto j = 0; j < attribute_count; j++) {
      const auto attribute_name_res = get_string(reader_.read_u16());
      if (!attribute_name_res) {
        return attribute_name_res.err();
      }
      const auto& attribute_name = attribute_name_res.value();
      const auto attribute_length_res = reader_.read_u32();
      if (!attribute_length_res) {
        return attribute_length_res.err();
      }
      const auto attribute_length = attribute_length_res.value();
      const auto attribute_start = reader_.cursor();
      const auto attribute_end = reader_.cursor() + attribute_length;
      if (attribute_name == "Signature") {
        const auto sig_index_res = reader_.read_u16();
        if (!sig_index_res) {
          goto fail;
        }
        const auto sig_index = sig_index_res.value();
        if (sig_index != 0) {
          const auto sig = get_string(sig_index);
          if (!sig) {
            goto fail;
          }
          component.signature = sig.value();
        }
      } else if (attribute_name == "RuntimeVisibleAnnotations") {
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        component.visible_annotations.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_annotation();
          if (!anno) {
            goto fail;
          }
          component.visible_annotations.emplace_back(anno.value());
        }
      } else if (attribute_name == "RuntimeInvisibleAnnotations") {
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        component.invisible_annotations.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_annotation();
          if (!anno) {
            goto fail;
          }
          component.invisible_annotations.emplace_back(anno.value());
        }
      } else if (attribute_name == "RuntimeVisibleTypeAnnotations") {
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        component.visible_type_annotations.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_type_annotation(dummy_labels);
          if (!anno) {
            goto fail;
          }
          component.visible_type_annotations.emplace_back(anno.value());
        }
      } else if (attribute_name == "RuntimeInvisibleTypeAnnotations") {
        const auto num_annotations_res = reader_.read_u16();
        if (!num_annotations_res) {
          goto fail;
        }
        const auto num_annotations = num_annotations_res.value();
        component.invisible_type_annotations.reserve(num_annotations);
        for (auto k = 0; k < num_annotations; k++) {
          const auto anno = read_type_annotation(dummy_labels);
          if (!anno) {
            goto fail;
          }
          component.invisible_type_annotations.emplace_back(anno.value());
        }
      } else {
      fail:
        auto data = reader_.bytes(attribute_start, attribute_end);
        if (!data) {
          return data.err();
        }
        component.attributes.emplace_back(attribute_name, data.value());
      }
      if (!reader_.cursor(attribute_end)) {
        return error("Failed to read record component attribute");
      }
    }
    file.record_components.emplace_back(std::move(component));
  }
  return {};
}
result<void> class_reader::read_header() {
  const auto magic_res = reader_.read_u32();
  if (const auto magic = magic_res.value(); magic != 0xcafebabe) {
    return error("Invalid magic number, expected 0xcafebabe got " + std::to_string(magic));
  }

  const auto minor_version_res = reader_.read_u16();
  if (!minor_version_res) {
    return minor_version_res.err();
  }
  const auto major_version_res = reader_.read_u16();
  if (!major_version_res) {
    return major_version_res.err();
  }
  const auto minor_version = minor_version_res.value();
  const auto major_version = major_version_res.value();

  class_version_ = major_version << 16 | minor_version;
  const auto constant_pool_count_res = reader_.read_u16();
  if (!constant_pool_count_res) {
    return constant_pool_count_res.err();
  }
  const auto constant_pool_count = constant_pool_count_res.value();
  pool_.reserve(constant_pool_count);
  pool_.emplace_back(cp::pad_info{});
  for (auto i = 1; i < constant_pool_count; i++) {
    const auto tag_res = reader_.read_u8();
    if (!tag_res) {
      return tag_res.err();
    }
    const auto tag = tag_res.value();
    switch (tag) {
      case cp::utf8_info::tag: {
        const auto utf = reader_.read_utf();
        if (!utf) {
          return utf.err();
        }
        pool_.emplace_back(cp::utf8_info{utf.value()});
        break;
      }
      case cp::integer_info::tag: {
        const auto value = reader_.read_i32();
        if (!value) {
          return value.err();
        }
        pool_.emplace_back(cp::integer_info{value.value()});
        break;
      }
      case cp::float_info::tag: {
        const auto value = reader_.read_f32();
        if (!value) {
          return value.err();
        }
        pool_.emplace_back(cp::float_info{value.value()});
        break;
      }
      case cp::long_info::tag: {
        const auto value = reader_.read_i64();
        if (!value) {
          return value.err();
        }
        pool_.emplace_back(cp::long_info{value.value()});
        pool_.emplace_back(cp::pad_info{});
        i++;
        break;
      }
      case cp::double_info::tag: {
        const auto value = reader_.read_f64();
        if (!value) {
          return value.err();
        }
        pool_.emplace_back(cp::double_info{value.value()});
        pool_.emplace_back(cp::pad_info{});
        i++;
        break;
      }
      case cp::class_info::tag: {
        const auto name_index = reader_.read_u16();
        if (!name_index) {
          return name_index.err();
        }
        pool_.emplace_back(cp::class_info{name_index.value()});
        break;
      }
      case cp::string_info::tag: {
        const auto string_index = reader_.read_u16();
        if (!string_index) {
          return string_index.err();
        }
        pool_.emplace_back(cp::string_info{string_index.value()});
        break;
      }
      case cp::field_ref_info::tag: {
        const auto class_index = reader_.read_u16();
        if (!class_index) {
          return class_index.err();
        }
        const auto name_and_type_index = reader_.read_u16();
        if (!name_and_type_index) {
          return name_and_type_index.err();
        }
        pool_.emplace_back(cp::field_ref_info{class_index.value(), name_and_type_index.value()});

        break;
      }
      case cp::method_ref_info::tag: {
        const auto class_index = reader_.read_u16();
        if (!class_index) {
          return class_index.err();
        }
        const auto name_and_type_index = reader_.read_u16();
        if (!name_and_type_index) {
          return name_and_type_index.err();
        }
        pool_.emplace_back(cp::method_ref_info{class_index.value(), name_and_type_index.value()});

        break;
      }

      case cp::interface_method_ref_info::tag: {
        const auto class_index = reader_.read_u16();
        if (!class_index) {
          return class_index.err();
        }
        const auto name_and_type_index = reader_.read_u16();
        if (!name_and_type_index) {
          return name_and_type_index.err();
        }
        pool_.emplace_back(cp::interface_method_ref_info{class_index.value(), name_and_type_index.value()});
        break;
      }
      case cp::name_and_type_info::tag: {
        const auto name_index = reader_.read_u16();
        if (!name_index) {
          return name_index.err();
        }
        const auto desc_index = reader_.read_u16();
        if (!desc_index) {
          return desc_index.err();
        }
        pool_.emplace_back(cp::name_and_type_info{name_index.value(), desc_index.value()});
        break;
      }
      case cp::method_handle_info::tag: {
        const auto reference_kind = reader_.read_u8();
        if (!reference_kind) {
          return reference_kind.err();
        }
        const auto reference_index = reader_.read_u16();
        if (!reference_index) {
          return reference_index.err();
        }
        pool_.emplace_back(cp::method_handle_info{reference_kind.value(), reference_index.value()});
        break;
      }
      case cp::method_type_info::tag: {
        const auto desc_index = reader_.read_u16();
        if (!desc_index) {
          return desc_index.err();
        }
        pool_.emplace_back(cp::method_type_info{desc_index.value()});
        break;
      }
      case cp::dynamic_info::tag: {
        const auto bootstrap_method_attr_index = reader_.read_u16();
        if (!bootstrap_method_attr_index) {
          return bootstrap_method_attr_index.err();
        }
        const auto name_and_type_index = reader_.read_u16();
        if (!name_and_type_index) {
          return name_and_type_index.err();
        }
        pool_.emplace_back(cp::dynamic_info{bootstrap_method_attr_index.value(), name_and_type_index.value()});
        break;
      }
      case cp::invoke_dynamic_info::tag: {
        const auto bootstrap_method_attr_index = reader_.read_u16();
        if (!bootstrap_method_attr_index) {
          return bootstrap_method_attr_index.err();
        }
        const auto name_and_type_index = reader_.read_u16();
        if (!name_and_type_index) {
          return name_and_type_index.err();
        }
        pool_.emplace_back(cp::invoke_dynamic_info{bootstrap_method_attr_index.value(), name_and_type_index.value()});
        break;
      }
      case cp::module_info::tag: {
        const auto name_index = reader_.read_u16();
        if (!name_index) {
          return name_index.err();
        }
        pool_.emplace_back(cp::module_info{name_index.value()});
        break;
      }
      case cp::package_info::tag: {
        const auto name_index = reader_.read_u16();
        if (!name_index) {
          return name_index.err();
        }
        pool_.emplace_back(cp::package_info{name_index.value()});
        break;
      }
      default:
        return error("Unknown constant pool tag (" + std::to_string(tag) + ")");
    }
  }
  return {};
}
result<annotation> class_reader::read_annotation() {
  const auto anno_name = get_string(reader_.read_u16());
  const auto num_pairs = reader_.read_u16();
  return anno_name.zip(
      [this](const std::string& name, uint16_t num_element_value_pairs) -> result<annotation> {
        annotation anno(name);
        anno.values.reserve(num_element_value_pairs);
        for (auto i = 0; i < num_element_value_pairs; i++) {
          const auto element_name = get_string(reader_.read_u16());
          if (!element_name) {
            return element_name.err();
          }
          const auto value = read_element_value();
          if (!value) {
            return value.err();
          }
          anno.values.emplace_back(element_name.value(), value.value());
        }
        return anno;
      },
      num_pairs);
}
result<type_annotation> class_reader::read_type_annotation(std::vector<std::pair<size_t, label>>& labels) {
  const auto target_type_res = reader_.read_u8();
  if (!target_type_res) {
    return target_type_res.err();
  }
  const auto target_type = target_type_res.value();
  type_annotation_target target = target::empty{};
  switch (target_type) {
    case 0x00:
    case 0x01: {
      const auto index = reader_.read_u8();
      if (!index) {
        return index.err();
      }
      target = target::type_parameter{index.value()};
      break;
    }
    case 0x10: {
      const auto index = reader_.read_u16();
      if (!index) {
        return index.err();
      }
      target = target::supertype{index.value()};
      break;
    }
    case 0x11:
    case 0x12: {
      const auto type_parameter_index = reader_.read_u8();
      if (!type_parameter_index) {
        return type_parameter_index.err();
      }
      const auto bound_index = reader_.read_u8();
      if (!bound_index) {
        return bound_index.err();
      }
      target = target::type_parameter_bound{type_parameter_index.value(), bound_index.value()};
      break;
    }
    case 0x13:
    case 0x14:
    case 0x15:
      target = target::empty();
      break;
    case 0x16: {
      const auto index = reader_.read_u8();
      if (!index) {
        return index.err();
      }
      target = target::formal_parameter{index.value()};
      break;
    }
    case 0x17: {
      const auto index = reader_.read_u16();
      if (!index) {
        return index.err();
      }
      target = target::throws{index.value()};
      break;
    }
    case 0x40:
    case 0x41: {
      const auto table_length_res = reader_.read_u16();
      if (!table_length_res) {
        return table_length_res.err();
      }
      const auto table_length = table_length_res.value();
      std::vector<target::local> table;
      table.reserve(table_length);
      for (auto i = 0; i < table_length; i++) {
        const auto start_pc_res = reader_.read_u16();
        if (!start_pc_res) {
          return start_pc_res.err();
        }
        const auto start_pc = start_pc_res.value();
        const auto end_pc_res = reader_.read_u16();
        if (!end_pc_res) {
          return end_pc_res.err();
        }
        const auto end_pc = end_pc_res.value() + start_pc;
        const auto start = get_label(labels, start_pc);
        const auto end = get_label(labels, end_pc);
        const auto index = reader_.read_u16();
        if (!index) {
          return index.err();
        }
        table.emplace_back(start, end, index.value());
      }
      target = target::localvar{table};
      break;
    }
    case 0x42: {
      const auto index = reader_.read_u16();
      if (!index) {
        return index.err();
      }
      target = target::catch_target{index.value()};
      break;
    }
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46: {
      const auto offset_res = reader_.read_u16();
      if (!offset_res) {
        return offset_res.err();
      }
      const auto offset = get_label(labels, offset_res.value());
      target = target::offset_target{offset};
      break;
    }
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B: {
      const auto offset_res = reader_.read_u16();
      if (!offset_res) {
        return offset_res.err();
      }
      const auto offset = get_label(labels, offset_res.value());
      const auto index = reader_.read_u8();
      if (!index) {
        return index.err();
      }
      target = target::type_argument{offset, index.value()};
      break;
    }
    default:
      return error("Unknown target type (" + std::to_string(target_type) + ")");
  }
  const auto target_path_length_res = reader_.read_u8();
  if (!target_path_length_res) {
    return target_path_length_res.err();
  }
  const auto target_path_length = target_path_length_res.value();
  std::vector<std::pair<uint8_t, uint8_t>> path;
  path.reserve(target_path_length);
  for (auto i = 0; i < target_path_length; i++) {
    const auto type_kind = reader_.read_u8();
    if (!type_kind) {
      return type_kind.err();
    }
    const auto type_index = reader_.read_u8();
    if (!type_index) {
      return type_index.err();
    }
    path.emplace_back(type_kind.value(), type_index.value());
  }
  const auto desc = get_string(reader_.read_u16());
  if (!desc) {
    return desc.err();
  }
  type_annotation anno(target_type, target, type_path(path), desc.value());
  const auto num_element_value_pairs_res = reader_.read_u16();
  if (!num_element_value_pairs_res) {
    return num_element_value_pairs_res.err();
  }
  const auto num_element_value_pairs = num_element_value_pairs_res.value();
  anno.values.reserve(num_element_value_pairs);
  for (auto i = 0; i < num_element_value_pairs; i++) {
    const auto element_name = get_string(reader_.read_u16());
    if (!element_name) {
      return element_name.err();
    }
    const auto value = read_element_value();
    if (!value) {
      return value.err();
    }
    anno.values.emplace_back(element_name.value(), value.value());
  }
  return anno;
}
result<element_value> class_reader::read_element_value() {
  const auto tag_res = reader_.read_u8();
  if (!tag_res) {
    return tag_res.err();
  }
  switch (const auto tag = tag_res.value(); tag) {
    case 'B':
      return get_int(reader_.read_u16()).map([](int32_t value) -> element_value {
        return {static_cast<int8_t>(value)};
      });
    case 'C':
      return get_int(reader_.read_u16()).map([](int32_t value) -> element_value {
        return {static_cast<uint16_t>(value)};
      });
    case 'I':
      return get_int(reader_.read_u16()).map([](int32_t value) -> element_value { return {value}; });
    case 'S':
      return get_int(reader_.read_u16()).map([](int32_t value) -> element_value {
        return {static_cast<int16_t>(value)};
      });
    case 'Z':
      return get_int(reader_.read_u16()).map([](int32_t value) -> element_value { return {value != 0}; });
    case 'D':
      return get_double(reader_.read_u16()).map([](double value) -> element_value { return {value}; });
    case 'F':
      return get_float(reader_.read_u16()).map([](float value) -> element_value { return {value}; });
    case 'J':
      return get_long(reader_.read_u16()).map([](int64_t value) -> element_value { return {value}; });
    case 's':
      return get_string(reader_.read_u16()).map([](const std::string& value) -> element_value { return {value}; });
    case 'e': {
      const auto name_res = get_string(reader_.read_u16());
      const auto type_name_res = get_string(reader_.read_u16());
      return name_res.lift(
          [](const std::string& name, const std::string& type_name) -> element_value {
            return {std::make_pair(name, type_name)};
          },
          type_name_res);
    }
    case 'c':
      return get_string(reader_.read_u16()).map([](const std::string& value) -> element_value {
        return {type(type_kind::object, value)};
      });
    case '@':
      return read_annotation().map([](annotation anno) -> element_value { return {anno}; });
    case '[': {
      const auto num_values_res = reader_.read_u16();
      if (!num_values_res) {
        return num_values_res.err();
      }
      const auto num_values = num_values_res.value();
      std::vector<element_value> values;
      values.reserve(num_values);
      for (auto i = 0; i < num_values; i++) {
        const auto val = read_element_value();
        if (!val) {
          return val.err();
        }
        values.emplace_back(val.value());
      }
      return element_value{values};
    }
    default:
      return error("Unknown element value tag (" + std::to_string(tag) + ")");
  }
}
result<frame_var> class_reader::read_frame_var(std::vector<std::pair<size_t, label>>& labels) {
  const auto tag_res = reader_.read_u8();
  if (!tag_res) {
    return tag_res.err();
  }
  switch (const auto tag = tag_res.value(); tag) {
    case 0:
      return {top_var()};
    case 1:
      return {int_var()};
    case 2:
      return {float_var()};
    case 5:
      return {null_var()};
    case 6:
      return {uninitialized_this_var()};
    case 7:
      return get_string(reader_.read_u16()).map([](const std::string& value) -> frame_var {
        return object_var(value);
      });
    case 8:
      return reader_.read_u16().map(
          [this, &labels](uint16_t index) -> frame_var { return uninitialized_var(get_label(labels, index)); });
    case 4:
      return {long_var()};
    case 3:
      return {double_var()};
    default:
      return error("Unknown frame var type (" + std::to_string(tag) + ")");
  }
}
label class_reader::get_label(std::vector<std::pair<size_t, label>>& labels, size_t offset) {
  for (const auto& [pos, lbl] : labels) {
    if (pos == offset) {
      return lbl;
    }
  }

  label l;
  label_count_++;
  auto i = label_count_;
  while (i > 0) {
    i--;
    const auto letter = static_cast<char>('A' + (i % 26));
    l.debug_name.insert(0, 1, letter);
    i /= 26;
  }
  labels.emplace_back(offset, l);
  return l;
}
std::optional<label> class_reader::get_label_opt(std::vector<std::pair<size_t, label>>& labels, size_t offset) {
  for (const auto& [pos, lbl] : labels) {
    if (pos == offset) {
      return lbl;
    }
  }
  return std::nullopt;
}
result<std::string> class_reader::get_string(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index (" + std::to_string(index) + ")");
  }
  if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
    return utf8->value;
  }
  if (const auto string = std::get_if<cp::string_info>(&pool_.at(index))) {
    index = string->string_index;
    if (index == 0 || index >= pool_.size()) {
      return error("Invalid constant pool index (" + std::to_string(index) + ")");
    }
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
      return utf8->value;
    }
  }
  if (const auto class_info = std::get_if<cp::class_info>(&pool_.at(index))) {
    index = class_info->name_index;
    if (index == 0 || index >= pool_.size()) {
      return error("Invalid constant pool index (" + std::to_string(index) + ")");
    }
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
      return utf8->value;
    }
  }
  if (const auto module_info = std::get_if<cp::module_info>(&pool_.at(index))) {
    index = module_info->name_index;
    if (index == 0 || index >= pool_.size()) {
      return error("Invalid constant pool index (" + std::to_string(index) + ")");
    }
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
      return utf8->value;
    }
  }
  if (const auto package_info = std::get_if<cp::package_info>(&pool_.at(index))) {
    index = package_info->name_index;
    if (index == 0 || index >= pool_.size()) {
      return error("Invalid constant pool index (" + std::to_string(index) + ")");
    }
    if (const auto utf8 = std::get_if<cp::utf8_info>(&pool_.at(index))) {
      return utf8->value;
    }
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<std::pair<std::string, std::string>> class_reader::get_name_and_type(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto name_and_type = std::get_if<cp::name_and_type_info>(&pool_.at(index))) {
    const auto name = get_string(name_and_type->name_index);
    if (!name) {
      return name.err();
    }
    const auto desc = get_string(name_and_type->desc_index);
    if (!desc) {
      return desc.err();
    }
    return std::make_pair(name.value(), desc.value());
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<std::tuple<std::string, std::string, std::string, bool>> class_reader::get_ref(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto method = std::get_if<cp::method_ref_info>(&pool_.at(index))) {
    const auto nat_res = get_name_and_type(method->name_and_type_index);
    if (!nat_res) {
      return nat_res.err();
    }
    const auto [name, desc] = nat_res.value();
    const auto owner = get_string(method->class_index);
    if (!owner) {
      return owner.err();
    }
    return std::make_tuple(owner.value(), name, desc, false);
  }
  if (const auto field = std::get_if<cp::field_ref_info>(&pool_.at(index))) {
    const auto nat_res = get_name_and_type(field->name_and_type_index);
    if (!nat_res) {
      return nat_res.err();
    }
    const auto [name, desc] = nat_res.value();
    const auto owner = get_string(field->class_index);
    if (!owner) {
      return owner.err();
    }
    return std::make_tuple(owner.value(), name, desc, false);
  }
  if (const auto interface_method = std::get_if<cp::interface_method_ref_info>(&pool_.at(index))) {
    const auto nat_res = get_name_and_type(interface_method->name_and_type_index);
    if (!nat_res) {
      return nat_res.err();
    }
    const auto [name, desc] = nat_res.value();
    const auto owner = get_string(interface_method->class_index);
    if (!owner) {
      return owner.err();
    }
    return std::make_tuple(owner.value(), name, desc, true);
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<method_handle> class_reader::get_method_handle(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto mh = std::get_if<cp::method_handle_info>(&pool_.at(index))) {
    const auto ref = get_ref(mh->reference_index);
    if (!ref) {
      return ref.err();
    }
    const auto [owner, name, desc, interface] = ref.value();
    return method_handle{mh->reference_kind, owner, name, desc, interface};
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<value> class_reader::get_constant(uint16_t index) {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto int_info = std::get_if<cp::integer_info>(&pool_.at(index))) {
    return {int_info->value};
  }
  if (const auto float_info = std::get_if<cp::float_info>(&pool_.at(index))) {
    return {float_info->value};
  }
  if (const auto long_info = std::get_if<cp::long_info>(&pool_.at(index))) {
    return {long_info->value};
  }
  if (const auto double_info = std::get_if<cp::double_info>(&pool_.at(index))) {
    return {double_info->value};
  }
  if (const auto cls = std::get_if<cp::class_info>(&pool_.at(index))) {
    return get_string(cls->name_index).map([](const std::string& v) -> value { return type(type_kind::object, v); });
  }
  if (const auto str = std::get_if<cp::string_info>(&pool_.at(index))) {
    return get_string(str->string_index).map([](const std::string& v) -> value { return v; });
  }
  if (const auto mh = std::get_if<cp::method_handle_info>(&pool_.at(index))) {
    const auto ref = get_ref(mh->reference_index);
    if (!ref) {
      return ref.err();
    }
    const auto [owner, name, desc, interface] = ref.value();
    return {method_handle(mh->reference_kind, owner, name, desc, interface)};
  }
  if (const auto mt = std::get_if<cp::method_type_info>(&pool_.at(index))) {
    return get_string(mt->desc_index).map([](const std::string& v) -> value { return method_type(v); });
  }
  if (const auto dyn = std::get_if<cp::dynamic_info>(&pool_.at(index))) {
    const auto curr = reader_.cursor();
    if (!reader_.cursor(bootstrap_methods_[dyn->bootstrap_method_attr_index])) {
      return error("Invalid bootstrap method index: " + std::to_string(dyn->bootstrap_method_attr_index));
    }
    const auto handle_res = get_method_handle(reader_.read_u16());
    if (!handle_res) {
      return handle_res.err();
    }
    const auto& handle = handle_res.value();
    const auto arg_count_res = reader_.read_u16();
    if (!arg_count_res) {
      return arg_count_res.err();
    }
    const auto arg_count = arg_count_res.value();
    std::vector<uint16_t> arg_indices;
    arg_indices.reserve(arg_count);
    for (auto i = 0; i < arg_count; i++) {
      const auto arg = reader_.read_u16();
      if (!arg) {
        return arg.err();
      }
      arg_indices.emplace_back(arg.value());
    }
    if (!reader_.cursor(curr)) {
      return error("Failed to reset cursor");
    }
    std::vector<value> args;
    args.reserve(arg_count);
    for (const auto arg_index : arg_indices) {
      const auto arg = get_constant(arg_index);
      if (!arg) {
        return arg.err();
      }
      args.emplace_back(arg.value());
    }
    const auto nat_res = get_name_and_type(dyn->name_and_type_index);
    if (!nat_res) {
      return nat_res.err();
    }
    const auto [name, desc] = nat_res.value();
    return {dynamic(name, desc, handle, args)};
  }
  return error("Invalid constant pool_ index: " + std::to_string(index));
}
result<int32_t> class_reader::get_int(uint16_t index) const {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto int_info = std::get_if<cp::integer_info>(&pool_.at(index))) {
    return int_info->value;
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<int64_t> class_reader::get_long(uint16_t index) const {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto long_info = std::get_if<cp::long_info>(&pool_.at(index))) {
    return long_info->value;
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<float> class_reader::get_float(uint16_t index) const {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto float_info = std::get_if<cp::float_info>(&pool_.at(index))) {
    return float_info->value;
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<double> class_reader::get_double(uint16_t index) const {
  if (index == 0 || index >= pool_.size()) {
    return error("Invalid constant pool index: " + std::to_string(index));
  }
  if (const auto double_info = std::get_if<cp::double_info>(&pool_.at(index))) {
    return double_info->value;
  }
  return error("Invalid constant pool index (" + std::to_string(index) + ")");
}
result<std::string> class_reader::get_string(result<uint16_t> index) {
  return index.and_then([this](uint16_t index) -> result<std::string> { return get_string(index); });
}
result<std::pair<std::string, std::string>> class_reader::get_name_and_type(result<uint16_t> index) {
  return index.and_then(
      [this](uint16_t index) -> result<std::pair<std::string, std::string>> { return get_name_and_type(index); });
}
result<std::tuple<std::string, std::string, std::string, bool>> class_reader::get_ref(result<uint16_t> index) {
  return index.and_then([this](uint16_t index) -> result<std::tuple<std::string, std::string, std::string, bool>> {
    return get_ref(index);
  });
}
result<method_handle> class_reader::get_method_handle(result<uint16_t> index) {
  return index.and_then([this](uint16_t index) -> result<method_handle> { return get_method_handle(index); });
}
result<value> class_reader::get_constant(result<uint16_t> index) {
  return index.and_then([this](uint16_t index) -> result<value> { return get_constant(index); });
}
result<int32_t> class_reader::get_int(result<uint16_t> index) const {
  return index.and_then([this](uint16_t index) -> result<int32_t> { return get_int(index); });
}
result<int64_t> class_reader::get_long(result<uint16_t> index) const {
  return index.and_then([this](uint16_t index) -> result<int64_t> { return get_long(index); });
}
result<float> class_reader::get_float(result<uint16_t> index) const {
  return index.and_then([this](uint16_t index) -> result<float> { return get_float(index); });
}
result<double> class_reader::get_double(result<uint16_t> index) const {
  return index.and_then([this](uint16_t index) -> result<double> { return get_double(index); });
}
} // namespace cafe
