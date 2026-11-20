/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include <cafe/ids.hpp>

namespace cafe {

namespace detail {
class ClassReader;
class SymbolicEdit;
} // namespace detail

/**
 * @brief Class-file constant-pool tags with their wire values.
 * @ingroup model
 */
enum class CpTag : std::uint8_t {
  Reserved = 0, ///< Reserved slot, not a usable constant.
  Utf8 = 1, ///< JVM CONSTANT_Utf8 tag.
  Integer = 3, ///< JVM CONSTANT_Integer tag.
  Float = 4, ///< JVM CONSTANT_Float tag.
  Long = 5, ///< JVM CONSTANT_Long tag.
  Double = 6, ///< JVM CONSTANT_Double tag.
  Class = 7, ///< JVM CONSTANT_Class tag.
  String = 8, ///< JVM CONSTANT_String tag.
  Fieldref = 9, ///< JVM CONSTANT_Fieldref tag.
  Methodref = 10, ///< JVM CONSTANT_Methodref tag.
  InterfaceMethodref = 11, ///< JVM CONSTANT_InterfaceMethodref tag.
  NameAndType = 12, ///< JVM CONSTANT_NameAndType tag.
  MethodHandle = 15, ///< JVM CONSTANT_MethodHandle tag.
  MethodType = 16, ///< JVM CONSTANT_MethodType tag.
  Dynamic = 17, ///< JVM CONSTANT_Dynamic tag.
  InvokeDynamic = 18, ///< JVM CONSTANT_InvokeDynamic tag.
  Module = 19, ///< JVM CONSTANT_Module tag.
  Package = 20, ///< JVM CONSTANT_Package tag.
};

/**
 * @brief Fixed-width constant-pool payload. The tag selects which fields are meaningful; UTF-8
 * bytes are held separately by ConstantPool.
 * @ingroup model
 *
 * first/second are wire indices in declaration order. For Dynamic and
 * InvokeDynamic first.value is a bootstrap table ordinal (zero is valid).
 * bits carries exact numeric bits or an internal UTF-8 record index; reference_kind is for
 * MethodHandle only.
 */
struct CpEntry {
  CpTag tag = CpTag::Reserved; ///< Class-file constant tag selecting the entry payload form.
  std::uint8_t reference_kind = 0; ///< MethodHandle reference kind (1 through 9); unused for other tags.
  CpIndex first; ///< First wire index; for Dynamic/InvokeDynamic this is a zero-based bootstrap ordinal.
  CpIndex second; ///< Second wire index, usually a descriptor or NameAndType reference.
  std::uint64_t bits = 0; ///< Exact numeric bits, or an internal UTF-8 record index for Utf8 entries.
};

/**
 * @brief Constant-pool lookup and append failures.
 * @ingroup model
 */
enum class CpError {
  InvalidIndex, ///< Index is zero, reserved, or outside the constant pool.
  WrongTag, ///< Constant tag does not match the requested operation.
  InvalidModifiedUtf8, ///< Bytes are not valid Java modified UTF-8.
  InvalidEntry, ///< Constant payload is invalid for its tag.
  PoolFull, ///< The constant-pool slot count would exceed the wire limit.
  StringTooLong, ///< Encoded UTF-8 payload exceeds the u2 byte-length limit.
};

/**
 * @brief Append-only constant-pool slots with stable wire indices and modified UTF-8 storage. Slot
 * zero and the second slot of long/double entries are reserved. Appending can invalidate entry
 * pointers; string views retain stable backing storage until pool destruction. Allocator resources
 * must outlive the pool; mutation requires external synchronization.
 * @ingroup model
 */
class ConstantPool {
public:
  /**
   * @brief Create an empty pool with reserved slot zero and caller-selected allocation resources.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @param string_resource Optional separate resource for stable string bytes; it must outlive the
   *   pool.
   * @pre resource is non-null.
   *
   * If supplied, both resources must outlive the pool.
   */
  explicit ConstantPool(std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
                        std::pmr::memory_resource* string_resource = nullptr);
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ConstantPool(const ConstantPool&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ConstantPool& operator=(const ConstantPool&) = delete;

  /**
   * @brief Read the constant-pool slot count.
   * @return The wire constant_pool_count, including slot zero and reserved wide-entry slots.
   *
   * Pointers returned by get() may be invalidated by appending. UTF-8 views
   * keep their backing storage until the owning class or pool is destroyed.
   */
  [[nodiscard]] std::size_t size() const noexcept;
  /**
   * @brief Look up a non-reserved constant slot.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return A borrowed entry pointer, or nullptr for zero, out-of-range, or reserved slots.
   */
  [[nodiscard]] const CpEntry* get(CpIndex index) const noexcept;
  /**
   * @brief Test a constant reference and tag.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @param tag Required constant-pool tag.
   * @return True only for an existing non-reserved entry of the requested tag.
   */
  [[nodiscard]] bool has(CpIndex index, CpTag tag) const noexcept;
  /**
   * @brief Look up a class or interface method reference.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return A borrowed entry pointer, or InvalidIndex/WrongTag.
   */
  [[nodiscard]] std::expected<const CpEntry*, CpError> get_method_ref(CpIndex index) const noexcept;
  /**
   * @brief Read original bytes of a UTF-8 constant without requiring valid encoding.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return A stable borrowed view, or InvalidIndex/WrongTag.
   */
  [[nodiscard]] std::expected<std::string_view, CpError> utf8_bytes(CpIndex index) const noexcept;
  /**
   * @brief Read a validated modified UTF-8 constant.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return A stable borrowed view, or an index, tag, or encoding error.
   */
  [[nodiscard]] std::expected<std::string_view, CpError> get_utf8(CpIndex index) const noexcept;
  /**
   * @brief Decode modified UTF-8 into Java UTF-16 code units.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return Owned UTF-16 text, preserving unpaired surrogates, or a CpError.
   */
  [[nodiscard]] std::expected<std::u16string, CpError> decode_utf8(CpIndex index) const;

  /**
   * @brief Append a fixed-width constant while preserving all existing indices.
   * @param entry Constant-pool payload to append.
   * @return Its new index, or a tag/entry/pool-capacity error. Cross-references are checked on
   *   write.
   *
   * Indices never change. Append supports fixed-width entries; use append_utf8
   * for MUTF-8. Cross-references may point forward and are checked on write.
   */
  [[nodiscard]] std::expected<CpIndex, CpError> append(CpEntry entry);
  /**
   * @brief Copy validated modified UTF-8 into stable owned storage.
   * @param modified_utf8 Java modified UTF-8 bytes to validate and copy.
   * @return Its new index, or an encoding, length, or pool-capacity error.
   */
  [[nodiscard]] std::expected<CpIndex, CpError> append_utf8(std::string_view modified_utf8);

private:
  friend class detail::ClassReader;
  friend class detail::SymbolicEdit;

  /**
   * @brief Borrowed modified UTF-8 bytes with their validation state.
   * @ingroup model
   */
  struct Utf8Value {
    std::string_view bytes; ///< Borrowed stable modified UTF-8 payload.
    bool valid; ///< Whether the modified UTF-8 bytes passed validation.
  };

  /**
   * @brief Append a UTF-8 view already owned by the class source.
   * @param bytes Borrowed modified UTF-8 view; its backing storage must outlive the pool.
   * @param valid Whether the borrowed modified UTF-8 bytes have passed validation.
   * @return The new index; caller supplies the known encoding validity.
   */
  [[nodiscard]] CpIndex append_utf8_view(std::string_view bytes, bool valid);
  /**
   * @brief Reserve constant-pool slots before publishing an append.
   * @param extra Additional constant-pool slots to reserve before publication.
   */
  void reserve_for(std::size_t extra);

  std::pmr::memory_resource* string_resource; ///< Borrowed optional allocator for stable string payloads.
  std::pmr::vector<CpEntry> entries; ///< Constant-pool slots including slot zero and wide-entry reserved slots.
  std::pmr::vector<Utf8Value> utf8_values; ///< UTF-8 payload records indexed by CpEntry::bits for Utf8 constants.
  /**
   * @brief Individually allocated stable buffers for programmatically appended UTF-8 values.
   *
   * Owning strings for programmatic appends. Individual buffers are stable
   * even for short strings because the payload is explicitly allocated.
   */
  std::pmr::vector<std::pmr::vector<char>> owned_strings;
};

} // namespace cafe
