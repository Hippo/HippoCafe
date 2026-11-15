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
}

enum class CpTag : std::uint8_t {
  Reserved = 0,
  Utf8 = 1,
  Integer = 3,
  Float = 4,
  Long = 5,
  Double = 6,
  Class = 7,
  String = 8,
  Fieldref = 9,
  Methodref = 10,
  InterfaceMethodref = 11,
  NameAndType = 12,
  MethodHandle = 15,
  MethodType = 16,
  Dynamic = 17,
  InvokeDynamic = 18,
  Module = 19,
  Package = 20,
};

// first/second are wire indices in declaration order. For Dynamic and
// InvokeDynamic first.value is a bootstrap table ordinal (zero is valid).
// bits carries exact numeric bits; reference_kind is for MethodHandle only.
struct CpEntry {
  CpTag tag = CpTag::Reserved;
  std::uint8_t reference_kind = 0;
  CpIndex first;
  CpIndex second;
  std::uint64_t bits = 0;
};

enum class CpError { InvalidIndex,
                     WrongTag,
                     InvalidModifiedUtf8,
                     InvalidEntry,
                     PoolFull,
                     StringTooLong };

class ConstantPool {
public:
  // If supplied, both resources must outlive the pool.
  explicit ConstantPool(std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
                        std::pmr::memory_resource* string_resource = nullptr);
  ConstantPool(const ConstantPool&) = delete;
  ConstantPool& operator=(const ConstantPool&) = delete;

  // Pointers returned by get() may be invalidated by appending. UTF-8 views
  // keep their backing storage until the owning class or pool is destroyed.
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const CpEntry* get(CpIndex index) const noexcept;
  [[nodiscard]] bool has(CpIndex index, CpTag tag) const noexcept;
  [[nodiscard]] std::expected<const CpEntry*, CpError> get_method_ref(CpIndex index) const noexcept;
  [[nodiscard]] std::expected<std::string_view, CpError> utf8_bytes(CpIndex index) const noexcept;
  [[nodiscard]] std::expected<std::string_view, CpError> get_utf8(CpIndex index) const noexcept;
  // Java UTF-16 code units, including unpaired surrogates when present.
  [[nodiscard]] std::expected<std::u16string, CpError> decode_utf8(CpIndex index) const;

  // Indices never change. Append supports fixed-width entries; use append_utf8
  // for MUTF-8. Cross-references may point forward and are checked on write.
  [[nodiscard]] std::expected<CpIndex, CpError> append(CpEntry entry);
  [[nodiscard]] std::expected<CpIndex, CpError> append_utf8(std::string_view modified_utf8);

private:
  friend class detail::ClassReader;

  struct Utf8Value {
    std::string_view bytes;
    bool valid;
  };

  [[nodiscard]] CpIndex append_utf8_view(std::string_view bytes, bool valid);
  void reserve_for(std::size_t extra);

  std::pmr::memory_resource* string_resource;
  std::pmr::vector<CpEntry> entries;
  std::pmr::vector<Utf8Value> utf8_values;
  // Owning strings for programmatic appends. Individual buffers are stable
  // even for short strings because the payload is explicitly allocated.
  std::pmr::vector<std::pmr::vector<char>> owned_strings;
};

} // namespace cafe
