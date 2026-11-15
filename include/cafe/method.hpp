#pragma once

#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/ids.hpp>

namespace cafe {

class Method {
public:
  Method(Method&&) noexcept = default;
  Method& operator=(Method&&) = delete;

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
  [[nodiscard]] const CodeAttribute* code() const noexcept;
  [[nodiscard]] const OpaqueCode* opaque_code() const noexcept;

private:
  friend class ClassFile;
  friend class MethodEditor;
  friend class detail::ClassReader;

  Method(std::uint16_t flags, CpIndex name, CpIndex descriptor, std::pmr::memory_resource* resource) : flags(flags), name(name), descriptor(descriptor), attribute_entries(resource) {
  }

  std::uint16_t flags;
  CpIndex name;
  CpIndex descriptor;
  std::pmr::vector<Attribute> attribute_entries;
};

// Borrows a method from its ClassFile. Acquire a fresh editor after adding
// or erasing a method.
class MethodEditor {
public:
  [[nodiscard]] const Method& method() const noexcept {
    return *member;
  }
  // Returns nullptr for methods without decoded Code.
  [[nodiscard]] CodeAttribute* code() const noexcept;

private:
  friend class ClassFile;
  explicit MethodEditor(Method& method) noexcept : member(&method) {
  }

  Method* member;
};

} // namespace cafe
