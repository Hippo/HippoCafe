#include "cafe/class_file/class_file.hpp"

#include "cafe/constants.hpp"
#include "cafe/data_rw.hpp"
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
  writer.write_u16(file.constant_pool.count() + 1);
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
} // namespace cafe
