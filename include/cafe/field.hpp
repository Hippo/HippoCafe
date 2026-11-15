/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/ids.hpp>

namespace cafe {

/**
 * @brief Field declaration owned by ClassFile. Its strings are constant-pool references and its
 * attributes retain class-file order. Adding or erasing fields can invalidate references to this
 * object.
 * @ingroup model
 */
class Field {
public:
  /**
   * @brief Move the declaration and transfer its attribute buffer. References to the source
   * declaration do not retarget to the destination.
   */
  Field(Field&&) noexcept = default;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Field& operator=(Field&&) = delete;

  /**
   * @brief Read the declaration's JVM access mask.
   * @return The declaration access bits.
   */
  [[nodiscard]] std::uint16_t access_flags() const noexcept {
    return flags;
  }
  /**
   * @brief Read the declaration name reference.
   * @return A UTF-8 constant-pool index owned by the containing class.
   */
  [[nodiscard]] CpIndex name_index() const noexcept {
    return name;
  }
  /**
   * @brief Read the declaration descriptor reference.
   * @return A UTF-8 constant-pool index containing the JVM descriptor.
   */
  [[nodiscard]] CpIndex descriptor_index() const noexcept {
    return descriptor;
  }
  /**
   * @brief Inspect ordered declaration attributes.
   * @return A borrowed span valid while this declaration remains alive and unchanged.
   */
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept {
    return attribute_entries;
  }

private:
  friend class ClassFile;
  friend class detail::ClassReader;

  /**
   * @brief Create an allocator-bound field declaration with constant-pool identity.
   * @param flags JVM access bit mask for the declaration.
   * @param name UTF-8 constant-pool reference for the name.
   * @param descriptor UTF-8 constant-pool reference for the descriptor.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  Field(std::uint16_t flags, CpIndex name, CpIndex descriptor, std::pmr::memory_resource* resource) : flags(flags), name(name), descriptor(descriptor), attribute_entries(resource) {
  }

  std::uint16_t flags; ///< JVM access bit mask.
  CpIndex name; ///< UTF-8 constant-pool reference for the declaration or attribute name.
  CpIndex descriptor; ///< UTF-8 constant-pool reference for the JVM descriptor.
  std::pmr::vector<Attribute> attribute_entries; ///< Owned ordered attribute list using the declaration's allocator.
};

} // namespace cafe
