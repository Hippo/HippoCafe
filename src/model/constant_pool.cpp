/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup implementation
 */

#include <cafe/constant_pool.hpp>

#include <algorithm>
#include <limits>

#include "modified_utf8.hpp"

namespace cafe {

namespace {
constexpr auto MAX_POOL_COUNT = std::numeric_limits<std::uint16_t>::max(); ///< Maximum class-file constant_pool_count including reserved slots.
}

ConstantPool::ConstantPool(std::pmr::memory_resource* resource, std::pmr::memory_resource* string_resource) : string_resource(string_resource ? string_resource : resource),
                                                                                                              entries(resource),
                                                                                                              utf8_values(resource),
                                                                                                              owned_strings(this->string_resource) {
  entries.emplace_back();
}

std::size_t ConstantPool::size() const noexcept {
  return entries.size();
}

const CpEntry* ConstantPool::get(CpIndex index) const noexcept {
  if (index.value >= entries.size() || entries[index.value].tag == CpTag::Reserved)
    return nullptr;
  return &entries[index.value];
}

bool ConstantPool::has(CpIndex index, CpTag tag) const noexcept {
  const auto* entry = get(index);
  return entry && entry->tag == tag;
}

std::expected<const CpEntry*, CpError> ConstantPool::get_method_ref(CpIndex index) const noexcept {
  const auto* entry = get(index);
  if (!entry)
    return std::unexpected(CpError::InvalidIndex);
  if (entry->tag != CpTag::Methodref && entry->tag != CpTag::InterfaceMethodref)
    return std::unexpected(CpError::WrongTag);
  return entry;
}

std::expected<std::string_view, CpError> ConstantPool::utf8_bytes(CpIndex index) const noexcept {
  const auto* entry = get(index);
  if (!entry)
    return std::unexpected(CpError::InvalidIndex);
  if (entry->tag != CpTag::Utf8)
    return std::unexpected(CpError::WrongTag);
  return utf8_values[static_cast<std::size_t>(entry->bits)].bytes;
}

std::expected<std::string_view, CpError> ConstantPool::get_utf8(CpIndex index) const noexcept {
  const auto bytes = utf8_bytes(index);
  if (!bytes)
    return bytes;
  if (!utf8_values[static_cast<std::size_t>(entries[index.value].bits)].valid)
    return std::unexpected(CpError::InvalidModifiedUtf8);
  return bytes;
}

std::expected<std::u16string, CpError> ConstantPool::decode_utf8(CpIndex index) const {
  const auto bytes = get_utf8(index);
  if (!bytes)
    return std::unexpected(bytes.error());
  std::u16string decoded;
  decoded.reserve(bytes->size());
  std::size_t cursor = 0;
  while (cursor < bytes->size()) {
    char16_t unit = 0;
    if (!detail::next_modified_utf8(*bytes, cursor, unit))
      return std::unexpected(CpError::InvalidModifiedUtf8);
    decoded.push_back(unit);
  }
  return decoded;
}

void ConstantPool::reserve_for(std::size_t extra) {
  if (extra > entries.capacity() - entries.size())
    entries.reserve(std::min<std::size_t>(MAX_POOL_COUNT, std::max(entries.size() + extra, entries.capacity() * 2)));
}

std::expected<CpIndex, CpError> ConstantPool::append(CpEntry entry) {
  switch (entry.tag) {
    case CpTag::Integer:
    case CpTag::Float:
      if (entry.bits > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(CpError::InvalidEntry);
      break;
    case CpTag::Long:
    case CpTag::Double:
    case CpTag::Class:
    case CpTag::String:
    case CpTag::Fieldref:
    case CpTag::Methodref:
    case CpTag::InterfaceMethodref:
    case CpTag::NameAndType:
    case CpTag::MethodHandle:
    case CpTag::MethodType:
    case CpTag::Dynamic:
    case CpTag::InvokeDynamic:
    case CpTag::Module:
    case CpTag::Package:
      break;
    default:
      return std::unexpected(CpError::InvalidEntry);
  }

  const bool two_slots = entry.tag == CpTag::Long || entry.tag == CpTag::Double;
  const std::size_t slots = two_slots ? 2 : 1;
  if (slots > MAX_POOL_COUNT - entries.size())
    return std::unexpected(CpError::PoolFull);
  reserve_for(slots);
  const CpIndex result{static_cast<std::uint16_t>(entries.size())};
  entries.push_back(entry);
  if (two_slots)
    entries.emplace_back();
  return result;
}

CpIndex ConstantPool::append_utf8_view(std::string_view bytes, bool valid) {
  reserve_for(1);
  CpEntry entry;
  entry.tag = CpTag::Utf8;
  entry.bits = utf8_values.size();
  utf8_values.push_back({bytes, valid});
  const CpIndex result{static_cast<std::uint16_t>(entries.size())};
  entries.push_back(entry);
  return result;
}

std::expected<CpIndex, CpError> ConstantPool::append_utf8(std::string_view modified_utf8) {
  if (modified_utf8.size() > MAX_POOL_COUNT)
    return std::unexpected(CpError::StringTooLong);
  if (entries.size() == MAX_POOL_COUNT)
    return std::unexpected(CpError::PoolFull);
  if (!detail::valid_modified_utf8(modified_utf8))
    return std::unexpected(CpError::InvalidModifiedUtf8);
  std::pmr::vector<char> buffer{string_resource};
  if (!modified_utf8.empty())
    buffer.assign(modified_utf8.begin(), modified_utf8.end());
  owned_strings.push_back(std::move(buffer));
  const auto& stored = owned_strings.back();
  const auto bytes = stored.empty() ? std::string_view{} : std::string_view(stored.data(), stored.size());
  try {
    return append_utf8_view(bytes, true);
  } catch (...) {
    owned_strings.pop_back();
    throw;
  }
}

} // namespace cafe
