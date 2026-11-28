/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup implementation
 */

#include <cafe/attribute.hpp>

#include <algorithm>

#include <memory>
#include <utility>

#include <cafe/class_file.hpp>

#include "class_storage.hpp"

namespace cafe {

RawAttribute::RawAttribute(SourceSlice slice, std::uint64_t revision) noexcept : storage(slice), pool_revision(revision) {
}

RawAttribute::RawAttribute(std::vector<std::byte> bytes) noexcept : storage(std::move(bytes)) {
}

RawAttribute::RawAttribute(SourceSlice source, std::vector<std::byte> bytes) : storage(std::make_shared<const RemappedPayload>(RemappedPayload{source, std::move(bytes)})) {
}

std::optional<std::span<const std::byte>> RawAttribute::bytes(const ClassFile& owner) const noexcept {
  return bytes(*owner.storage);
}

// Storage is only constructed, copied, or moved. All alternatives have nonthrowing
// moves, so variant stages a potentially throwing vector copy before changing alternatives.
// NOLINTNEXTLINE(bugprone-exception-escape)
std::optional<std::span<const std::byte>> RawAttribute::bytes(const detail::ClassStorage& owner) const noexcept {
  if (pool_revision && pool_revision != owner.pool_revision)
    return std::nullopt;
  if (const auto* remapped = std::get_if<std::shared_ptr<const RemappedPayload>>(&storage))
    return *remapped ? std::optional{std::span<const std::byte>((*remapped)->bytes)} : std::nullopt;
  if (const auto* slice = source_slice()) {
    const std::span<const std::byte> source(owner.source);
    if (slice->offset > source.size() || slice->length > source.size() - slice->offset)
      return std::nullopt;
    return source.subspan(slice->offset, slice->length);
  }
  return std::span<const std::byte>(std::get<std::vector<std::byte>>(storage));
}

const SourceSlice* RawAttribute::source_slice() const noexcept {
  if (const auto* remapped = std::get_if<std::shared_ptr<const RemappedPayload>>(&storage))
    return *remapped ? &(*remapped)->source : nullptr;
  return std::get_if<SourceSlice>(&storage);
}

Attribute::Attribute(CpIndex name, AttributeKind kind, Payload payload) noexcept
    : name(name), attribute_kind(kind), payload(std::move(payload)) {
}

Attribute::Attribute(CpIndex name, SourceFile value) noexcept : Attribute(name, AttributeKind::SourceFile, value) {
}
Attribute::Attribute(CpIndex name, Signature value) noexcept : Attribute(name, AttributeKind::Signature, value) {
}
Attribute::Attribute(CpIndex name, ConstantValue value) noexcept : Attribute(name, AttributeKind::ConstantValue, value) {
}
Attribute::Attribute(CpIndex name, Synthetic value) noexcept : Attribute(name, AttributeKind::Synthetic, value) {
}
Attribute::Attribute(CpIndex name, Deprecated value) noexcept : Attribute(name, AttributeKind::Deprecated, value) {
}

Attribute Attribute::raw(CpIndex name, std::vector<std::byte> bytes) noexcept {
  return Attribute(name, AttributeKind::Unknown, RawAttribute(std::move(bytes)));
}

Attribute Attribute::exceptions(CpIndex name, std::span<const CpIndex> types) {
  Exceptions list{std::pmr::vector<CpIndex>(std::pmr::new_delete_resource())};
  if (!types.empty())
    list.types.assign(types.begin(), types.end());
  return Attribute(name, AttributeKind::Exceptions, std::move(list));
}

Attribute Attribute::bootstrap_methods(CpIndex name, std::span<const BootstrapMethod> methods) {
  auto* resource = std::pmr::new_delete_resource();
  BootstrapMethods table{std::pmr::vector<BootstrapMethod>(resource)};
  table.methods.reserve(methods.size());
  for (const auto& method : methods) {
    table.methods.push_back(BootstrapMethod{
        method.method_handle, std::pmr::vector<CpIndex>(method.arguments, resource)});
  }
  return Attribute(name, AttributeKind::BootstrapMethods, std::move(table));
}

Attribute Attribute::new_code(CpIndex name, detail::ClassStorage& owner) {
  std::pmr::polymorphic_allocator<CodeAttribute> allocator(&owner.budget);
  CodeAttribute* value = allocator.allocate(1);
  try {
    ::new (static_cast<void*>(value)) CodeAttribute(owner);
  } catch (...) {
    allocator.deallocate(value, 1);
    throw;
  }
  return Attribute(name, AttributeKind::Code, CodeOwner(value));
}

void Attribute::bind_pool(std::uint64_t revision) noexcept {
  if (auto* raw = std::get_if<RawAttribute>(&payload))
    raw->pool_revision = revision;
}

void Attribute::CodeDeleter::operator()(CodeAttribute* value) const noexcept {
  std::pmr::polymorphic_allocator<CodeAttribute> allocator(value->nested_attributes.get_allocator().resource());
  std::destroy_at(value);
  allocator.deallocate(value, 1);
}

CodeAttribute* Attribute::mutable_code() noexcept {
  const auto* owner = std::get_if<CodeOwner>(&payload);
  return owner ? owner->get() : nullptr;
}

CodeAttribute::CodeAttribute(detail::ClassStorage& owner) :
    owner(owner), instructions(&owner.budget), handlers(&owner.budget), nested_attributes(&owner.budget),
    original_boundaries(&owner.budget) {
}

void CodeAttribute::set_exception_handlers(std::span<const ExceptionHandler> new_handlers) {
  if (std::ranges::equal(handlers, new_handlers))
    return;
  std::pmr::vector<ExceptionHandler> replacement(handlers.get_allocator());
  if (!new_handlers.empty())
    replacement.assign(new_handlers.begin(), new_handlers.end());
  if (!original_handlers)
    original_handlers.emplace(handlers, handlers.get_allocator());
  handlers.swap(replacement);
  supplied_metadata.reset();
  metadata_revision = detail::next_declaration_revision();
}

void CodeAttribute::supply_metadata(std::uint16_t max_stack, std::uint16_t max_locals,
                                    std::span<const ExceptionHandler> new_handlers,
                                    std::vector<Attribute> new_attributes, bool preserve_original_encodings) {
  std::pmr::vector<ExceptionHandler> updated_handlers(handlers.get_allocator());
  if (!new_handlers.empty())
    updated_handlers.assign(new_handlers.begin(), new_handlers.end());
  std::pmr::vector<Attribute> updated_attributes(nested_attributes.get_allocator());
  updated_attributes.reserve(new_attributes.size());
  for (auto& attribute : new_attributes) {
    attribute.bind_pool(owner.pool_revision);
    updated_attributes.push_back(std::move(attribute));
  }

  handlers.swap(updated_handlers);
  nested_attributes.swap(updated_attributes);
  original_boundaries.clear();
  owned_origin_code_size.reset();
  original_handlers.reset();
  stack_limit = max_stack;
  locals_limit = max_locals;
  supplied_metadata = SuppliedMetadata{instructions.revision(), preserve_original_encodings};
  metadata_revision = detail::next_declaration_revision();
}

const CodeAttribute* Method::code() const noexcept {
  for (const auto& attribute : attribute_entries)
    if (const auto* code = attribute.get<CodeAttribute>())
      return code;
  return nullptr;
}

const OpaqueCode* Method::opaque_code() const noexcept {
  for (const auto& attribute : attribute_entries)
    if (const auto* code = attribute.get<OpaqueCode>())
      return code;
  return nullptr;
}

CodeAttribute* MethodEditor::code() const noexcept {
  for (auto& attribute : member->attribute_entries)
    if (auto* code = attribute.mutable_code())
      return code;
  return nullptr;
}

std::expected<LabelId, CodeEditError> MethodEditor::label_before(InstructionId instruction) {
  auto* body = code();
  if (!body)
    return std::unexpected(CodeEditError::NoCode);
  return body->code().make_label(CodePosition{instruction});
}

std::expected<LabelId, CodeEditError> MethodEditor::label_at_end() {
  auto* body = code();
  if (!body)
    return std::unexpected(CodeEditError::NoCode);
  return body->code().make_label(CodePosition{});
}

} // namespace cafe
