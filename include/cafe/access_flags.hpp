/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>

namespace cafe {

/**
 * @brief JVM declaration access bits; overlapping values have context-specific meanings.
 * @ingroup model
 */
enum class AccessFlag : std::uint16_t {
  Public = 0x0001, ///< JVM ACC_PUBLIC bit; interpret in the declaration context.
  Private = 0x0002, ///< JVM ACC_PRIVATE bit; interpret in the declaration context.
  Protected = 0x0004, ///< JVM ACC_PROTECTED bit; interpret in the declaration context.
  Static = 0x0008, ///< JVM ACC_STATIC bit; interpret in the declaration context.
  Final = 0x0010, ///< JVM ACC_FINAL bit; interpret in the declaration context.
  Super = 0x0020, ///< JVM ACC_SUPER bit; interpret in the declaration context.
  Synchronized = 0x0020, ///< JVM ACC_SYNCHRONIZED bit; interpret in the declaration context.
  Volatile = 0x0040, ///< JVM ACC_VOLATILE bit; interpret in the declaration context.
  Bridge = 0x0040, ///< JVM ACC_BRIDGE bit; interpret in the declaration context.
  Transient = 0x0080, ///< JVM ACC_TRANSIENT bit; interpret in the declaration context.
  Varargs = 0x0080, ///< JVM ACC_VARARGS bit; interpret in the declaration context.
  Native = 0x0100, ///< JVM ACC_NATIVE bit; interpret in the declaration context.
  Interface = 0x0200, ///< JVM ACC_INTERFACE bit; interpret in the declaration context.
  Abstract = 0x0400, ///< JVM ACC_ABSTRACT bit; interpret in the declaration context.
  Strict = 0x0800, ///< JVM ACC_STRICT bit; interpret in the declaration context.
  Synthetic = 0x1000, ///< JVM ACC_SYNTHETIC bit; interpret in the declaration context.
  Annotation = 0x2000, ///< JVM ACC_ANNOTATION bit; interpret in the declaration context.
  Enum = 0x4000, ///< JVM ACC_ENUM bit; interpret in the declaration context.
  Module = 0x8000, ///< JVM ACC_MODULE bit; interpret in the declaration context.
};

/**
 * @brief Test whether a JVM access bit is present.
 * @param flags JVM access bit mask for the declaration.
 * @param flag Access bit to test in the supplied mask.
 * @return True when the requested bit is set; interpretation depends on the declaration context.
 * @ingroup model
 */
[[nodiscard]] constexpr bool has_flag(std::uint16_t flags, AccessFlag flag) noexcept {
  return (flags & static_cast<std::uint16_t>(flag)) != 0;
}

} // namespace cafe
