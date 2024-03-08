#include "cafe/class_file.hpp"

#include <string>

#include "cafe/constants.hpp"
#include "cafe/data_rw.hpp"
#include "composer.hpp"
#include "parser.hpp"
#include "visitor.hpp"

namespace cafe {

std::istream& operator>>(std::istream& stream, class_file& file) {
  data_reader reader(stream);
  class_parser parser([](const std::exception&) {});
  parser.parse(reader, file);
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const class_file& file) {
  data_writer writer(stream);
  writer.write_u32(file.magic);
  writer.write_u16(file.minor_version);
  writer.write_u16(file.major_version);
  writer.write_u16(static_cast<uint16_t>(file.constant_pool.size()));
  for (const auto& cp_info : file.constant_pool) {
    std::visit(constant_pool_visitor(writer), cp_info);
  }
  writer.write_u16(file.access_flags);
  writer.write_u16(file.this_class);
  writer.write_u16(file.super_class);
  writer.write_u16(static_cast<uint16_t>(file.interfaces.size()));
  for (const auto& index : file.interfaces) {
    writer.write_u16(index);
  }
  const auto oak = (static_cast<uint32_t>(file.major_version) << 16 | file.minor_version) < class_version::v1_1;
  writer.write_u16(static_cast<uint16_t>(file.fields.size()));
  for (const auto& field : file.fields) {
    writer.write_u16(field.access_flags);
    writer.write_u16(field.name_index);
    writer.write_u16(field.descriptor_index);
    writer.write_u16(static_cast<uint16_t>(field.attributes.size()));
    for (const auto& attribute : field.attributes) {
      std::visit(attribute_visitor(writer, file.constant_pool, oak), attribute);
    }
  }
  writer.write_u16(static_cast<uint16_t>(file.methods.size()));
  for (const auto& method : file.methods) {
    writer.write_u16(method.access_flags);
    writer.write_u16(method.name_index);
    writer.write_u16(method.descriptor_index);
    writer.write_u16(static_cast<uint16_t>(method.attributes.size()));
    for (const auto& attribute : method.attributes) {
      std::visit(attribute_visitor(writer, file.constant_pool, oak), attribute);
    }
  }
  writer.write_u16(static_cast<uint16_t>(file.attributes.size()));
  for (const auto& attribute : file.attributes) {
    std::visit(attribute_visitor(writer, file.constant_pool, oak), attribute);
  }

  return stream;
}

field_model::field_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
}
method_model::method_model(uint16_t access_flags, const std::string_view& name, const std::string_view& descriptor) :
    access_flags(access_flags), name(name), descriptor(descriptor) {
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


std::istream& operator>>(std::istream& stream, class_model& model) {
  class_file cf;
  stream >> cf;
  class_composer composer(cf, [](const std::exception&) {});
  composer.compose(model);
  return stream;
}
} // namespace cafe
