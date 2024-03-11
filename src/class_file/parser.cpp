#include "parser.hpp"

#include <sstream>
#include "cafe/constants.hpp"


namespace cafe {
using at = attribute::attribute_type;

static const std::unordered_map<std::string, attribute::attribute_type> g_attribute_map = {
    {"ConstantValue", at::constant_value},
    {"Code", at::code},
    {"Exceptions", at::exceptions},
    {"SourceFile", at::source_file},
    {"LineNumberTable", at::line_number_table},
    {"LocalVariableTable", at::local_variable_table},
    {"InnerClasses", at::inner_classes},
    {"Synthetic", at::synthetic},
    {"Deprecated", at::deprecated},
    {"EnclosingMethod", at::enclosing_method},
    {"Signature", at::signature},
    {"SourceDebugExtension", at::source_debug_extension},
    {"LocalVariableTypeTable", at::local_variable_type_table},
    {"RuntimeVisibleAnnotations", at::runtime_visible_annotations},
    {"RuntimeInvisibleAnnotations", at::runtime_invisible_annotations},
    {"RuntimeVisibleParameterAnnotations", at::runtime_visible_parameter_annotations},
    {"RuntimeInvisibleParameterAnnotations", at::runtime_invisible_parameter_annotations},
    {"AnnotationDefault", at::annotation_default},
    {"StackMapTable", at::stack_map_table},
    {"BootstrapMethods", at::bootstrap_methods},
    {"RuntimeVisibleTypeAnnotations", at::runtime_visible_type_annotations},
    {"RuntimeInvisibleTypeAnnotations", at::runtime_invisible_type_annotations},
    {"MethodParameters", at::method_parameters},
    {"Module", at::module},
    {"ModulePackages", at::module_package},
    {"ModuleMainClass", at::module_main_class},
    {"NestHost", at::nest_host},
    {"NestMembers", at::nest_members},
    {"Record", at::record},
    {"PermittedSubclasses", at::permitted_subclasses}};

class_parser::class_parser(const std::function<void(const std::exception&)>& error_handler) :
    error_handler_(error_handler) {
}

cp::constant_pool class_parser::parse_constant_pool(data_reader& reader) {
  cp::constant_pool pool;
  const auto count = reader.read_u16();
  pool.reserve(count);
  pool.emplace_back(cp::pad_info{});
  for (auto i = 1; i < count; i++) {
    switch (const auto tag = reader.read_u8(); tag) {
      case cp::utf8_info::tag:
        pool.emplace_back(cp::utf8_info{reader.read_utf()});
        break;
      case cp::integer_info::tag:
        pool.emplace_back(cp::integer_info{reader.read_i32()});
        break;
      case cp::float_info::tag:
        pool.emplace_back(cp::float_info{reader.read_f32()});
        break;
      case cp::long_info::tag:
        pool.emplace_back(cp::long_info{reader.read_i64()});
        pool.emplace_back(cp::pad_info{});
        i++;
        break;
      case cp::double_info::tag:
        pool.emplace_back(cp::double_info{reader.read_f64()});
        pool.emplace_back(cp::pad_info{});
        i++;
        break;
      case cp::class_info::tag:
        pool.emplace_back(cp::class_info{reader.read_u16()});
        break;
      case cp::string_info::tag:
        pool.emplace_back(cp::string_info{reader.read_u16()});
        break;
      case cp::field_ref_info::tag:
        pool.emplace_back(cp::field_ref_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::method_ref_info::tag:
        pool.emplace_back(cp::method_ref_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::interface_method_ref_info::tag:
        pool.emplace_back(cp::interface_method_ref_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::name_and_type_info::tag:
        pool.emplace_back(cp::name_and_type_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::method_handle_info::tag:
        pool.emplace_back(cp::method_handle_info{reader.read_u8(), reader.read_u16()});
        break;
      case cp::method_type_info::tag:
        pool.emplace_back(cp::method_type_info{reader.read_u16()});
        break;
      case cp::dynamic_info::tag:
        pool.emplace_back(cp::dynamic_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::invoke_dynamic_info::tag:
        pool.emplace_back(cp::invoke_dynamic_info{reader.read_u16(), reader.read_u16()});
        break;
      case cp::module_info::tag:
        pool.emplace_back(cp::module_info{reader.read_u16()});
        break;
      case cp::package_info::tag:
        pool.emplace_back(cp::package_info{reader.read_u16()});
        break;
      default:
        throw std::runtime_error("Unknown constant pool tag (" + std::to_string(tag) + ")");
    }
  }
  return pool;
}
attribute::type_annotation class_parser::parse_type_annotation(data_reader& reader) {
  attribute::type_annotation annotation;
  const auto target_type = reader.read_u8();
  annotation.target_type = target_type;
  switch (target_type) {
    case 0x00:
    case 0x01:
      annotation.target_info = attribute::type_parameter{reader.read_u8()};
      break;
    case 0x10:
      annotation.target_info = attribute::supertype{reader.read_u16()};
      break;
    case 0x11:
    case 0x12:
      annotation.target_info = attribute::type_parameter_bound{reader.read_u8(), reader.read_u8()};
      break;
    case 0x13:
    case 0x14:
    case 0x15:
      annotation.target_info = attribute::empty{};
      break;
    case 0x16:
      annotation.target_info = attribute::formal_parameter{reader.read_u8()};
      break;
    case 0x17:
      annotation.target_info = attribute::throws{reader.read_u16()};
      break;
    case 0x40:
    case 0x41: {
      const auto table_length = reader.read_u16();
      std::vector<attribute::localvar::local> table;
      table.reserve(table_length);
      for (auto i = 0; i < table_length; i++) {
        table.push_back({reader.read_u16(), reader.read_u16(), reader.read_u16()});
      }
      annotation.target_info = attribute::localvar{std::move(table)};
      break;
    }
    case 0x42:
      annotation.target_info = attribute::catch_target{reader.read_u16()};
      break;
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
      annotation.target_info = attribute::offset_target{reader.read_u16()};
      break;
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B:
      annotation.target_info = attribute::type_argument{reader.read_u16(), reader.read_u8()};
      break;
    default:
      throw std::runtime_error("Unknown type annotation target type");
  }
  const auto path_length = reader.read_u8();
  std::vector<attribute::type_path::path> paths;
  paths.reserve(path_length);
  for (auto i = 0; i < path_length; i++) {
    paths.push_back({reader.read_u8(), reader.read_u8()});
  }
  annotation.target_path = attribute::type_path{std::move(paths)};
  annotation.type_index = reader.read_u16();
  const auto num_element_value_pairs = reader.read_u16();
  std::vector<attribute::element_pair> element_value_pairs;
  element_value_pairs.reserve(num_element_value_pairs);
  for (auto i = 0; i < num_element_value_pairs; i++) {
    element_value_pairs.push_back({reader.read_u16(), parse_element_value(reader)});
  }
  annotation.elements = std::move(element_value_pairs);
  return annotation;
}
attribute::annotation class_parser::parse_annotation(data_reader& reader) {
  const auto type_index = reader.read_u16();
  const auto num_element_value_pairs = reader.read_u16();
  std::vector<attribute::element_pair> element_value_pairs;
  element_value_pairs.reserve(num_element_value_pairs);
  for (auto i = 0; i < num_element_value_pairs; i++) {
    element_value_pairs.push_back({reader.read_u16(), parse_element_value(reader)});
  }
  return {type_index, std::move(element_value_pairs)};
}
attribute::element_value class_parser::parse_element_value(data_reader& reader) {
  switch (const auto tag = reader.read_u8(); tag) {
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'I':
    case 'J':
    case 's':
    case 'c':
      return {tag, reader.read_u16()};
    case 'e':
      return {tag, attribute::element_value::enum_value{reader.read_u16(), reader.read_u16()}};
    case '@':
      return {tag, parse_annotation(reader)};
    case '[': {
      const auto num_values = reader.read_u16();
      std::vector<attribute::element_value> values;
      values.reserve(num_values);
      for (auto i = 0; i < num_values; i++) {
        values.emplace_back(parse_element_value(reader));
      }
      return {tag, std::move(values)};
    }
    default:
      throw std::runtime_error("Unknown element value tag");
  }
}
attribute::stack_map_frame class_parser::parse_stack_map_frame(data_reader& reader) {
  if (const auto frame_type = reader.read_u8(); frame_type <= 63) {
    return attribute::same_frame{frame_type};
  } else if (frame_type <= 127) {
    return attribute::same_locals_1_stack_item_frame{frame_type, parse_verification_type_info(reader)};
  } else if (frame_type == 247) {
    return attribute::same_locals_1_stack_item_frame_extended{reader.read_u16(), parse_verification_type_info(reader)};
  } else if (frame_type >= 248 && frame_type <= 250) {
    auto delta = reader.read_u16();
    return attribute::chop_frame{frame_type, delta};
  } else if (frame_type == 251) {
    return attribute::same_frame_extended{reader.read_u16()};
  } else if (frame_type >= 252 && frame_type <= 254) {
    const auto offset_delta = reader.read_u16();
    const auto number_of_locals = frame_type - 251;
    std::vector<attribute::verification_type_info> locals;
    locals.reserve(number_of_locals);
    for (auto i = 0; i < number_of_locals; i++) {
      locals.emplace_back(parse_verification_type_info(reader));
    }
    return attribute::append_frame{frame_type, offset_delta, std::move(locals)};
  } else if (frame_type == 255) {
    const auto offset_delta = reader.read_u16();
    const auto number_of_locals = reader.read_u16();
    std::vector<attribute::verification_type_info> locals;
    locals.reserve(number_of_locals);
    for (auto i = 0; i < number_of_locals; i++) {
      locals.emplace_back(parse_verification_type_info(reader));
    }
    const auto number_of_stack_items = reader.read_u16();
    std::vector<attribute::verification_type_info> stack;
    stack.reserve(number_of_stack_items);
    for (auto i = 0; i < number_of_stack_items; i++) {
      stack.emplace_back(parse_verification_type_info(reader));
    }
    return attribute::full_frame{offset_delta, std::move(locals), std::move(stack)};
  }
  throw std::runtime_error("Unknown stack map frame type");
}
attribute::verification_type_info class_parser::parse_verification_type_info(data_reader& reader) {
  switch (reader.read_u8()) {
    case attribute::top_variable_info::tag:
      return attribute::top_variable_info{};
    case attribute::integer_variable_info::tag:
      return attribute::integer_variable_info{};
    case attribute::float_variable_info::tag:
      return attribute::float_variable_info{};
    case attribute::double_variable_info::tag:
      return attribute::double_variable_info{};
    case attribute::long_variable_info::tag:
      return attribute::long_variable_info{};
    case attribute::null_variable_info::tag:
      return attribute::null_variable_info{};
    case attribute::uninitialized_this_variable_info::tag:
      return attribute::uninitialized_this_variable_info{};
    case attribute::object_variable_info::tag:
      return attribute::object_variable_info{reader.read_u16()};
    case attribute::uninitialized_variable_info::tag:
      return attribute::uninitialized_variable_info{reader.read_u16()};
    default:
      throw std::runtime_error("Unknown verification type info tag");
  }
}
attribute::attribute class_parser::parse_attribute(data_reader& reader, const cp::constant_pool& pool, bool oak) {
  const auto name_index = reader.read_u16();
  const auto length = reader.read_u32();
  std::vector<uint8_t> info;
  reader.read_bytes(info, length);
  try {
    if (const auto name = std::get_if<cp::utf8_info>(&pool[name_index])) {
      const auto attr_type = g_attribute_map.at(name->value);
      const std::string str(info.begin(), info.end());
      std::istringstream iss(str, std::ios::binary);
      data_reader r(iss);
      switch (attr_type) {
        case at::constant_value:
          return {attribute::constant_value{r.read_u16()}};
        case at::code: {
          const auto max_stack = oak ? static_cast<uint16_t>(r.read_u8()) : r.read_u16();
          const auto max_locals = oak ? static_cast<uint16_t>(r.read_u8()) : r.read_u16();
          const auto code_length = oak ? r.read_u16() : r.read_u32();
          std::vector<uint8_t> bytecode;
          r.read_bytes(bytecode, code_length);
          const auto exception_table_length = r.read_u16();
          std::vector<attribute::code::exception> exceptions;
          exceptions.reserve(exception_table_length);
          for (auto i = 0; i < exception_table_length; i++) {
            exceptions.push_back({r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16()});
          }
          const auto attributes_count = r.read_u16();
          std::vector<attribute::attribute> attributes;
          attributes.reserve(attributes_count);
          for (auto i = 0; i < attributes_count; i++) {
            attributes.emplace_back(parse_attribute(r, pool, oak));
          }
          return {attribute::code{max_stack, max_locals, std::move(bytecode), std::move(exceptions),
                                  std::move(attributes)}};
        }
        case at::stack_map_table: {
          const auto number_of_entries = r.read_u16();
          std::vector<attribute::stack_map_frame> entries;
          entries.reserve(number_of_entries);
          for (auto i = 0; i < number_of_entries; i++) {
            entries.emplace_back(parse_stack_map_frame(r));
          }
          return {attribute::stack_map_table{std::move(entries)}};
        }
        case at::exceptions: {
          std::vector<uint16_t> exception_index_table;
          r.read_shorts(exception_index_table, r.read_u16());
          return {attribute::exceptions{std::move(exception_index_table)}};
        }
        case at::inner_classes: {
          const auto number_of_classes = r.read_u16();
          std::vector<attribute::inner_classes::inner_class> classes;
          classes.reserve(number_of_classes);
          for (auto i = 0; i < number_of_classes; i++) {
            classes.push_back({r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16()});
          }
          return {attribute::inner_classes{std::move(classes)}};
        }
        case at::enclosing_method:
          return {attribute::enclosing_method{r.read_u16(), r.read_u16()}};
        case at::synthetic:
          return {attribute::synthetic{}};
        case at::signature:
          return {attribute::signature{r.read_u16()}};
        case at::source_file:
          return {attribute::source_file{r.read_u16()}};
        case at::source_debug_extension:
          return {attribute::source_debug_extension{std::move(info)}};
        case at::line_number_table: {
          const auto line_number_table_length = r.read_u16();
          std::vector<attribute::line_number_table::line_number> line_number_table;
          line_number_table.reserve(line_number_table_length);
          for (auto i = 0; i < line_number_table_length; i++) {
            line_number_table.push_back({r.read_u16(), r.read_u16()});
          }
          return {attribute::line_number_table{std::move(line_number_table)}};
        }
        case at::local_variable_table: {
          const auto local_variable_table_length = r.read_u16();
          std::vector<attribute::local_variable_table::local_variable> local_variable_table;
          local_variable_table.reserve(local_variable_table_length);
          for (auto i = 0; i < local_variable_table_length; i++) {
            local_variable_table.push_back({r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16()});
          }
          return {attribute::local_variable_table{std::move(local_variable_table)}};
        }
        case at::local_variable_type_table: {
          const auto local_variable_type_table_length = r.read_u16();
          std::vector<attribute::local_variable_type_table::local_variable> local_variable_type_table;
          local_variable_type_table.reserve(local_variable_type_table_length);
          for (auto i = 0; i < local_variable_type_table_length; i++) {
            local_variable_type_table.push_back({r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16(), r.read_u16()});
          }
          return {attribute::local_variable_type_table{std::move(local_variable_type_table)}};
        }
        case at::deprecated:
          return {attribute::deprecated{}};
        case at::runtime_visible_annotations: {
          const auto num_annotations = r.read_u16();
          std::vector<attribute::annotation> annotations;
          annotations.reserve(num_annotations);
          for (auto i = 0; i < num_annotations; i++) {
            annotations.emplace_back(parse_annotation(r));
          }
          return {attribute::runtime_visible_annotations{std::move(annotations)}};
        }
        case at::runtime_invisible_annotations: {
          const auto num_annotations = r.read_u16();
          std::vector<attribute::annotation> annotations;
          annotations.reserve(num_annotations);
          for (auto i = 0; i < num_annotations; i++) {
            annotations.emplace_back(parse_annotation(r));
          }
          return {attribute::runtime_invisible_annotations{std::move(annotations)}};
        }
        case at::runtime_visible_parameter_annotations: {
          const auto num_parameters = r.read_u8();
          std::vector<std::vector<attribute::annotation>> parameter_annotations;
          parameter_annotations.reserve(num_parameters);
          for (auto i = 0; i < num_parameters; i++) {
            const auto num_annotations = r.read_u16();
            std::vector<attribute::annotation> annotations;
            annotations.reserve(num_annotations);
            for (auto j = 0; j < num_annotations; j++) {
              annotations.emplace_back(parse_annotation(r));
            }
            parameter_annotations.emplace_back(std::move(annotations));
          }
          return {attribute::runtime_visible_parameter_annotations{std::move(parameter_annotations)}};
        }
        case at::runtime_invisible_parameter_annotations: {
          const auto num_parameters = r.read_u8();
          std::vector<std::vector<attribute::annotation>> parameter_annotations;
          parameter_annotations.reserve(num_parameters);
          for (auto i = 0; i < num_parameters; i++) {
            const auto num_annotations = r.read_u16();
            std::vector<attribute::annotation> annotations;
            annotations.reserve(num_annotations);
            for (auto j = 0; j < num_annotations; j++) {
              annotations.emplace_back(parse_annotation(r));
            }
            parameter_annotations.emplace_back(std::move(annotations));
          }
          return {attribute::runtime_invisible_parameter_annotations{std::move(parameter_annotations)}};
        }
        case at::runtime_visible_type_annotations: {
          const auto num_annotations = r.read_u16();
          std::vector<attribute::type_annotation> annotations;
          annotations.reserve(num_annotations);
          for (auto i = 0; i < num_annotations; i++) {
            annotations.emplace_back(parse_type_annotation(r));
          }
          return {attribute::runtime_visible_type_annotations{std::move(annotations)}};
        }
        case at::runtime_invisible_type_annotations: {
          const auto num_annotations = r.read_u16();
          std::vector<attribute::type_annotation> annotations;
          annotations.reserve(num_annotations);
          for (auto i = 0; i < num_annotations; i++) {
            annotations.emplace_back(parse_type_annotation(r));
          }
          return {attribute::runtime_invisible_type_annotations{std::move(annotations)}};
        }
        case at::annotation_default:
          return {attribute::annotation_default{parse_element_value(r)}};
        case at::bootstrap_methods: {
          const auto num_bootstrap_methods = r.read_u16();
          std::vector<attribute::bootstrap_methods::bootstrap_method> bootstrap_methods;
          bootstrap_methods.reserve(num_bootstrap_methods);
          for (auto i = 0; i < num_bootstrap_methods; i++) {
            const auto bootstrap_method_ref = r.read_u16();
            const auto num_bootstrap_arguments = r.read_u16();
            std::vector<uint16_t> bootstrap_arguments;
            r.read_shorts(bootstrap_arguments, num_bootstrap_arguments);
            bootstrap_methods.push_back({bootstrap_method_ref, std::move(bootstrap_arguments)});
          }
          return {attribute::bootstrap_methods{std::move(bootstrap_methods)}};
        }
        case at::method_parameters: {
          const auto parameters_count = r.read_u8();
          std::vector<attribute::method_parameters::parameter> parameters;
          parameters.reserve(parameters_count);
          for (auto i = 0; i < parameters_count; i++) {
            parameters.push_back({r.read_u16(), r.read_u16()});
          }
          return {attribute::method_parameters{std::move(parameters)}};
        }
        case at::module: {
          const auto module_name_index = r.read_u16();
          const auto module_flags = r.read_u16();
          const auto module_version_index = r.read_u16();

          const auto requires_count = r.read_u16();
          std::vector<attribute::module::require>
            requires;
          requires.reserve(requires_count);
          for (auto i = 0; i < requires_count; i++) {
            requires.push_back({r.read_u16(), r.read_u16(), r.read_u16()});
          }

          const auto exports_count = r.read_u16();
          std::vector<attribute::module::mod_export> exports;
          exports.reserve(exports_count);
          for (auto i = 0; i < exports_count; i++) {
            const auto exports_index = r.read_u16();
            const auto exports_flags = r.read_u16();
            const auto exports_to_count = r.read_u16();
            std::vector<uint16_t> exports_to;
            r.read_shorts(exports_to, exports_to_count);
            exports.push_back({exports_index, exports_flags, std::move(exports_to)});
          }

          const auto opens_count = r.read_u16();
          std::vector<attribute::module::open> opens;
          opens.reserve(opens_count);
          for (auto i = 0; i < opens_count; i++) {
            const auto opens_index = r.read_u16();
            const auto opens_flags = r.read_u16();
            const auto opens_to_count = r.read_u16();
            std::vector<uint16_t> opens_to;
            r.read_shorts(opens_to, opens_to_count);
            opens.push_back({opens_index, opens_flags, std::move(opens_to)});
          }

          const auto uses_count = r.read_u16();
          std::vector<uint16_t> uses;
          r.read_shorts(uses, uses_count);

          const auto provides_count = r.read_u16();
          std::vector<attribute::module::provide> provides;
          provides.reserve(provides_count);
          for (auto i = 0; i < provides_count; i++) {
            const auto provides_index = r.read_u16();
            const auto provides_with_count = r.read_u16();
            std::vector<uint16_t> provides_with;
            r.read_shorts(provides_with, provides_with_count);
            provides.push_back({provides_index, std::move(provides_with)});
          }

          return {attribute::module{module_name_index, module_flags, module_version_index, std::move(requires),
                                    std::move(exports), std::move(opens), std::move(uses), std::move(provides)}};
        }
        case at::module_package: {
          std::vector<uint16_t> package_indices;
          r.read_shorts(package_indices, r.read_u16());
          return {attribute::module_packages{std::move(package_indices)}};
        }
        case at::module_main_class:
          return {attribute::module_main_class{r.read_u16()}};
        case at::nest_host:
          return {attribute::nest_host{r.read_u16()}};
        case at::nest_members: {
          std::vector<uint16_t> classes;
          r.read_shorts(classes, r.read_u16());
          return {attribute::nest_members{std::move(classes)}};
        }
        case at::record: {
          const auto components_count = r.read_u16();
          std::vector<attribute::record::component> components;
          components.reserve(components_count);
          for (auto i = 0; i < components_count; i++) {
            const auto n_index = r.read_u16();
            const auto descriptor_index = r.read_u16();
            const auto num_attributes = r.read_u16();
            std::vector<attribute::attribute> attributes;
            attributes.reserve(num_attributes);
            for (auto j = 0; j < num_attributes; j++) {
              attributes.emplace_back(parse_attribute(r, pool, oak));
            }
            components.push_back({n_index, descriptor_index, std::move(attributes)});
          }
          return {attribute::record{std::move(components)}};
        }
        case at::permitted_subclasses: {
          std::vector<uint16_t> classes;
          r.read_shorts(classes, r.read_u16());
          return {attribute::permitted_subclasses{std::move(classes)}};
        }
      }
    }
  } catch (const std::exception& e) {
    error_handler_(e);
  }
  return {attribute::unknown{name_index, std::move(info)}};
}

void class_parser::parse(data_reader& reader, class_file& file) {
  const auto magic = reader.read_u32();
  const auto minor_version = reader.read_u16();
  const auto major_version = reader.read_u16();
  const auto oak = (static_cast<uint32_t>(major_version) << 16 | minor_version) < class_version::v1_1;
  auto constant_pool = parse_constant_pool(reader);
  const auto access_flags = reader.read_u16();
  const auto this_class = reader.read_u16();
  const auto super_class = reader.read_u16();
  const auto interfaces_count = reader.read_u16();
  std::vector<uint16_t> interfaces;
  interfaces.reserve(interfaces_count);
  for (auto i = 0; i < interfaces_count; i++) {
    interfaces.emplace_back(reader.read_u16());
  }
  const auto fields_count = reader.read_u16();
  std::vector<field_info> fields;
  fields.reserve(fields_count);
  for (auto i = 0; i < fields_count; i++) {
    const auto access = reader.read_u16();
    const auto name_index = reader.read_u16();
    const auto descriptor_index = reader.read_u16();
    const auto attributes_count = reader.read_u16();
    std::vector<attribute::attribute> attributes;
    attributes.reserve(attributes_count);
    for (auto j = 0; j < attributes_count; j++) {
      attributes.emplace_back(parse_attribute(reader, constant_pool, oak));
    }
    fields.push_back({access, name_index, descriptor_index, std::move(attributes)});
  }
  const auto methods_count = reader.read_u16();
  std::vector<method_info> methods;
  methods.reserve(methods_count);
  for (auto i = 0; i < methods_count; i++) {
    const auto access = reader.read_u16();
    const auto name_index = reader.read_u16();
    const auto descriptor_index = reader.read_u16();
    const auto attributes_count = reader.read_u16();
    std::vector<attribute::attribute> attributes;
    attributes.reserve(attributes_count);
    for (auto j = 0; j < attributes_count; j++) {
      attributes.emplace_back(parse_attribute(reader, constant_pool, oak));
    }
    methods.push_back({access, name_index, descriptor_index, std::move(attributes)});
  }
  const auto attributes_count = reader.read_u16();
  std::vector<attribute::attribute> attributes;
  attributes.reserve(attributes_count);
  for (auto i = 0; i < attributes_count; i++) {
    attributes.emplace_back(parse_attribute(reader, constant_pool, oak));
  }

  file.magic = magic;
  file.minor_version = minor_version;
  file.major_version = major_version;
  file.constant_pool = std::move(constant_pool);
  file.access_flags = access_flags;
  file.this_class = this_class;
  file.super_class = super_class;
  file.interfaces = std::move(interfaces);
  file.fields = std::move(fields);
  file.methods = std::move(methods);
  file.attributes = std::move(attributes);
}

} // namespace cafe
