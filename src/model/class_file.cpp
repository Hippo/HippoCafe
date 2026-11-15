/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup implementation
 */

#include <cafe/class_file.hpp>

#include <limits>
#include <type_traits>
#include <utility>

#include "../detail/member_signature.hpp"
#include "class_storage.hpp"

namespace cafe {

namespace {
constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max(); ///< Largest u2 table count accepted by the class-file format.

/**
 * @brief Find the first declaration with exact modified UTF-8 name and descriptor.
 * @param entries Ordered entries or metadata records for this operation.
 * @param pool Owning class constant pool used for reference validation.
 * @param name Borrowed JVM name in modified UTF-8.
 * @param descriptor JVM field or method descriptor, or its UTF-8 constant-pool index.
 * @return Its table index, or nullopt when absent.
 * @ingroup implementation
 */
template<typename Member>
std::optional<std::size_t> find_member(std::span<const Member> entries, const ConstantPool& pool,
                                       std::string_view name, std::string_view descriptor) noexcept {
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& member = entries[index];
    if (pool.get_utf8(member.name_index()) == name && pool.get_utf8(member.descriptor_index()) == descriptor)
      return index;
  }
  return std::nullopt;
}

/**
 * @brief Rebuild an allocator-bound declaration table before committing an ordered erase.
 * @param entries Ordered entries or metadata records for this operation.
 * @param index Zero-based index in the corresponding table or slot array.
 * @ingroup implementation
 */
template<typename Member>
void erase_member(std::pmr::vector<Member>& entries, std::size_t index) {
  // Allocator-bound members support move construction. Reserve before moving
  // any elements, then publish the retained entries in declaration order.
  static_assert(std::is_nothrow_move_constructible_v<Member>);
  std::pmr::vector<Member> retained(entries.get_allocator());
  retained.reserve(entries.size() - 1);
  for (std::size_t i = 0; i < entries.size(); ++i)
    if (i != index)
      retained.push_back(std::move(entries[i]));
  entries.swap(retained);
}

} // namespace

ClassFile::ClassFile(std::unique_ptr<detail::ClassStorage> storage) noexcept : storage(std::move(storage)) {
}
ClassFile::~ClassFile() = default;
ClassFile::ClassFile(ClassFile&&) noexcept = default;

std::expected<ClassFile, ModelError> ClassFile::create(
    ClassVersion version, std::string_view name, std::optional<std::string_view> super_name,
    std::uint16_t flags, std::pmr::memory_resource* upstream) {
  if (version.major < 45 || !version.has_legal_minor())
    return std::unexpected(ModelError::InvalidVersion);
  const bool module = version.major >= 53 && has_flag(flags, AccessFlag::Module);
  if ((module && name != "module-info") || (module || name == "java/lang/Object") == super_name.has_value() ||
      !detail::valid_class_name(name) || (super_name && !detail::valid_class_name(*super_name)))
    return std::unexpected(ModelError::InvalidIdentity);

  auto storage = std::make_unique<detail::ClassStorage>(std::vector<std::byte>{}, upstream);
  storage->version = version;
  storage->flags = flags;
  auto add_class = [&](std::string_view class_name) -> std::expected<CpIndex, ModelError> {
    const auto text = storage->pool.append_utf8(class_name);
    if (!text)
      return std::unexpected(ModelError::InvalidIdentity);
    CpEntry entry;
    entry.tag = CpTag::Class;
    entry.first = *text;
    const auto index = storage->pool.append(entry);
    if (!index)
      return std::unexpected(ModelError::InvalidIdentity);
    return *index;
  };
  const auto this_class = add_class(name);
  if (!this_class)
    return std::unexpected(this_class.error());
  storage->this_class = *this_class;
  if (super_name) {
    const auto super_class = add_class(*super_name);
    if (!super_class)
      return std::unexpected(super_class.error());
    storage->super_class = *super_class;
  }
  return ClassFile(std::move(storage));
}

ClassVersion ClassFile::version() const noexcept {
  return storage->version;
}
std::uint16_t ClassFile::access_flags() const noexcept {
  return storage->flags;
}
CpIndex ClassFile::this_class() const noexcept {
  return storage->this_class;
}
CpIndex ClassFile::super_class() const noexcept {
  return storage->super_class;
}
ClassValidity ClassFile::validity() const noexcept {
  return storage->validity;
}
std::uint64_t ClassFile::declaration_revision() const noexcept {
  return storage->declaration_stamp;
}
ConstantPool& ClassFile::constant_pool() noexcept {
  return storage->pool;
}
const ConstantPool& ClassFile::constant_pool() const noexcept {
  return storage->pool;
}
std::span<const CpIndex> ClassFile::interfaces() const noexcept {
  return storage->interface_entries;
}
std::span<const Field> ClassFile::fields() const noexcept {
  return storage->field_entries;
}
std::span<const Method> ClassFile::methods() const noexcept {
  return storage->method_entries;
}
std::span<const Attribute> ClassFile::attributes() const noexcept {
  return storage->attribute_entries;
}
std::span<const std::byte> ClassFile::source_bytes() const noexcept {
  return storage->source;
}

std::optional<std::span<const std::byte>> ClassFile::source_slice(SourceSlice slice) const noexcept {
  const auto data = source_bytes();
  if (slice.offset > data.size() || slice.length > data.size() - slice.offset)
    return std::nullopt;
  return data.subspan(slice.offset, slice.length);
}

std::optional<std::size_t> ClassFile::find_field(std::string_view name, std::string_view descriptor) const noexcept {
  return find_member(fields(), constant_pool(), name, descriptor);
}

std::optional<std::size_t> ClassFile::find_method(std::string_view name, std::string_view descriptor) const noexcept {
  return find_member(methods(), constant_pool(), name, descriptor);
}

std::expected<void, ModelError> ClassFile::add_interface(CpIndex index) {
  if (interfaces().size() == MAX_COUNT)
    return std::unexpected(ModelError::LimitExceeded);
  if (!constant_pool().has(index, CpTag::Class))
    return std::unexpected(ModelError::InvalidReference);
  storage->interface_entries.push_back(index);
  storage->declaration_stamp = detail::next_declaration_revision();
  return {};
}

std::expected<std::size_t, ModelError> ClassFile::add_field(
    std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes) {
  if (fields().size() == MAX_COUNT || attributes.size() > MAX_COUNT)
    return std::unexpected(ModelError::LimitExceeded);
  const auto name_text = constant_pool().get_utf8(name);
  const auto descriptor_text = constant_pool().get_utf8(descriptor);
  if (!name_text || !descriptor_text || detail::field_signature_issue(*name_text, *descriptor_text))
    return std::unexpected(ModelError::InvalidReference);
  Field field(flags, name, descriptor, &storage->budget);
  field.attribute_entries.reserve(attributes.size());
  for (auto& attribute : attributes)
    field.attribute_entries.push_back(std::move(attribute));
  storage->field_entries.push_back(std::move(field));
  storage->declaration_stamp = detail::next_declaration_revision();
  return fields().size() - 1;
}

std::expected<std::size_t, ModelError> ClassFile::add_method(
    std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes) {
  if (methods().size() == MAX_COUNT || attributes.size() > MAX_COUNT)
    return std::unexpected(ModelError::LimitExceeded);
  const auto name_text = constant_pool().get_utf8(name);
  const auto descriptor_text = constant_pool().get_utf8(descriptor);
  if (!name_text || !descriptor_text || detail::method_signature_issue(version(), access_flags(), flags, *name_text, *descriptor_text))
    return std::unexpected(ModelError::InvalidReference);
  Method method(flags, name, descriptor, &storage->budget);
  method.attribute_entries.reserve(attributes.size());
  for (auto& attribute : attributes)
    method.attribute_entries.push_back(std::move(attribute));
  storage->method_entries.push_back(std::move(method));
  storage->declaration_stamp = detail::next_declaration_revision();
  return methods().size() - 1;
}

std::expected<void, ModelError> ClassFile::erase_field(std::size_t index) {
  if (index >= fields().size())
    return std::unexpected(ModelError::InvalidField);
  erase_member(storage->field_entries, index);
  storage->declaration_stamp = detail::next_declaration_revision();
  return {};
}

std::expected<void, ModelError> ClassFile::erase_method(std::size_t index) {
  if (index >= methods().size())
    return std::unexpected(ModelError::InvalidMethod);
  erase_member(storage->method_entries, index);
  storage->declaration_stamp = detail::next_declaration_revision();
  return {};
}

std::expected<void, ModelError> ClassFile::add_attribute(Attribute value) {
  if (attributes().size() == MAX_COUNT)
    return std::unexpected(ModelError::LimitExceeded);
  if (!constant_pool().get_utf8(value.name_index()))
    return std::unexpected(ModelError::InvalidReference);
  storage->attribute_entries.push_back(std::move(value));
  return {};
}

std::expected<std::reference_wrapper<CodeAttribute>, ModelError>
ClassFile::add_code(std::size_t method_index, CpIndex name) {
  if (method_index >= methods().size())
    return std::unexpected(ModelError::InvalidMethod);
  const auto& method = methods()[method_index];
  if (!detail::needs_code(method.access_flags(), *constant_pool().get_utf8(method.name_index())))
    return std::unexpected(ModelError::InvalidMethod);
  const auto attribute_name = constant_pool().get_utf8(name);
  if (!attribute_name || *attribute_name != "Code")
    return std::unexpected(ModelError::InvalidReference);
  auto& attributes = storage->method_entries[method_index].attribute_entries;
  if (attributes.size() == MAX_COUNT)
    return std::unexpected(ModelError::LimitExceeded);
  for (const auto& attribute : attributes) {
    const auto name = constant_pool().get_utf8(attribute.name_index());
    if (name && *name == "Code")
      return std::unexpected(ModelError::DuplicateCode);
  }

  auto& attribute = attributes.emplace_back(Attribute::new_code(name, &storage->budget));
  return std::ref(*attribute.mutable_code());
}

std::optional<MethodEditor> ClassFile::edit_method(std::size_t index) noexcept {
  if (index >= methods().size())
    return std::nullopt;
  return MethodEditor(storage->method_entries[index]);
}

CodeAttribute* ClassFile::edit_code(std::size_t method_index) noexcept {
  const auto editor = edit_method(method_index);
  return editor ? editor->code() : nullptr;
}

} // namespace cafe
