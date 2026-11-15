#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <cafe/access_flags.hpp>
#include <cafe/class_version.hpp>
#include <cafe/constant_pool.hpp>
#include <cafe/field.hpp>
#include <cafe/method.hpp>

namespace cafe {

namespace detail {
struct ClassStorage;
}

enum class ClassValidity { ParsedClean,
                           ParsedRecovered,
                           OpaqueEssentialContent,
                           Programmatic };
enum class ModelError { InvalidVersion,
                        InvalidIdentity,
                        InvalidReference,
                        InvalidMethod,
                        DuplicateCode,
                        LimitExceeded,
                        InvalidField };

class ClassFile {
public:
  // Names use internal JVM spelling and modified UTF-8. For Object and
  // module-info, pass a null super_name. Upstream must outlive the class.
  [[nodiscard]] static std::expected<ClassFile, ModelError>
  create(ClassVersion version, std::string_view name,
         std::optional<std::string_view> super_name = "java/lang/Object",
         std::uint16_t access_flags = 0x0021,
         std::pmr::memory_resource* upstream = std::pmr::get_default_resource());

  ~ClassFile();
  ClassFile(ClassFile&&) noexcept;
  ClassFile& operator=(ClassFile&&) = delete;
  ClassFile(const ClassFile&) = delete;
  ClassFile& operator=(const ClassFile&) = delete;

  [[nodiscard]] ClassVersion version() const noexcept;
  [[nodiscard]] std::uint16_t access_flags() const noexcept;
  [[nodiscard]] CpIndex this_class() const noexcept;
  [[nodiscard]] CpIndex super_class() const noexcept;
  [[nodiscard]] ClassValidity validity() const noexcept;
  [[nodiscard]] ConstantPool& constant_pool() noexcept;
  [[nodiscard]] const ConstantPool& constant_pool() const noexcept;

  // Adding or erasing entries can invalidate views into the corresponding
  // list. Views of source bytes remain valid until class destruction.
  [[nodiscard]] std::span<const CpIndex> interfaces() const noexcept;
  [[nodiscard]] std::span<const Field> fields() const noexcept;
  [[nodiscard]] std::span<const Method> methods() const noexcept;
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept;
  [[nodiscard]] std::span<const std::byte> source_bytes() const noexcept;
  [[nodiscard]] std::optional<std::span<const std::byte>> source_slice(SourceSlice slice) const noexcept;

  // Return the current table index of the first exact match. Names and
  // descriptors use JVM modified UTF-8.
  [[nodiscard]] std::optional<std::size_t> find_field(std::string_view name, std::string_view descriptor) const noexcept;
  [[nodiscard]] std::optional<std::size_t> find_method(std::string_view name, std::string_view descriptor) const noexcept;

  [[nodiscard]] std::expected<void, ModelError> add_interface(CpIndex index);
  [[nodiscard]] std::expected<std::size_t, ModelError> add_field(
      std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes = {});
  [[nodiscard]] std::expected<std::size_t, ModelError> add_method(
      std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes = {});
  // Erase a declaration in table order. Later indices shift down. The caller
  // updates any code that references the removed declaration.
  [[nodiscard]] std::expected<void, ModelError> erase_field(std::size_t index);
  [[nodiscard]] std::expected<void, ModelError> erase_method(std::size_t index);
  [[nodiscard]] std::expected<void, ModelError> add_attribute(Attribute value);
  [[nodiscard]] std::expected<std::reference_wrapper<CodeAttribute>, ModelError>
  add_code(std::size_t method, CpIndex name);
  [[nodiscard]] std::optional<MethodEditor> edit_method(std::size_t index) noexcept;
  [[nodiscard]] CodeAttribute* edit_code(std::size_t method) noexcept;

private:
  friend class detail::ClassReader;
  explicit ClassFile(std::unique_ptr<detail::ClassStorage> storage) noexcept;
  std::unique_ptr<detail::ClassStorage> storage;
};

} // namespace cafe
