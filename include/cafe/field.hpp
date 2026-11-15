#pragma once

#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/ids.hpp>

namespace cafe {

class Field {
public:
  Field(Field&&) noexcept = default;
  Field& operator=(Field&&) = delete;

  [[nodiscard]] std::uint16_t access_flags() const noexcept {
    return flags;
  }
  [[nodiscard]] CpIndex name_index() const noexcept {
    return name;
  }
  [[nodiscard]] CpIndex descriptor_index() const noexcept {
    return descriptor;
  }
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept {
    return attribute_entries;
  }

private:
  friend class ClassFile;
  friend class detail::ClassReader;

  Field(std::uint16_t flags, CpIndex name, CpIndex descriptor, std::pmr::memory_resource* resource) : flags(flags), name(name), descriptor(descriptor), attribute_entries(resource) {
  }

  std::uint16_t flags;
  CpIndex name;
  CpIndex descriptor;
  std::pmr::vector<Attribute> attribute_entries;
};

} // namespace cafe
