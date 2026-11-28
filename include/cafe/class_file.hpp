/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <cafe/access_flags.hpp>
#include <cafe/bytecode/instruction_view.hpp>
#include <cafe/class_version.hpp>
#include <cafe/constant_pool.hpp>
#include <cafe/error.hpp>
#include <cafe/field.hpp>
#include <cafe/method.hpp>

namespace cafe {

namespace detail {
class ClassPoolCompaction;
struct ClassStorage;
} // namespace detail

/**
 * @brief Model provenance and recovery state, independent of JVM verifier acceptance.
 * @ingroup model
 */
enum class ClassValidity {
  ParsedClean, ///< Parsed without compatible recovery.
  ParsedRecovered, ///< Parsed with reported recoveries; output may differ from the input.
  OpaqueEssentialContent, ///< Contains preserved undecodable essential Code.
  Programmatic, ///< Constructed through model-editing APIs rather than parsed input.
};

/**
 * @brief Owns a JVM class declaration, its source bytes, constant pool, and editable members. Move
 * construction preserves storage addresses; the moved-from object may only be destroyed. Borrowed
 * views require this storage to remain alive. Concurrent mutation requires external
 * synchronization. Upstream allocation failures may propagate as std::bad_alloc.
 * @ingroup model
 */
class ClassFile {
public:
  /**
   * @brief Create a programmatic class and its identity constants.
   *
   * Names use modified UTF-8 internal JVM spelling. Pass nullopt for the superclass of
   * java/lang/Object and module-info. The upstream resource must outlive the class.
   * @param version Exact JVM class-file major and minor version.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param super_name Direct superclass internal name; nullopt denotes no superclass.
   * @param access_flags JVM class access bits; the default describes a public ACC_SUPER class.
   * @param upstream Upstream PMR resource that must outlive the owning class; null selects the
   *   default resource.
   * @return A new owned class, or ModelError for invalid version, name, or superclass identity.
   */
  [[nodiscard]] static std::expected<ClassFile, ModelError>
  create(ClassVersion version, std::string_view name,
         std::optional<std::string_view> super_name = "java/lang/Object",
         std::uint16_t access_flags = 0x0021,
         std::pmr::memory_resource* upstream = std::pmr::get_default_resource());

  /**
   * @brief Release owned storage; previously borrowed views become invalid.
   */
  ~ClassFile();
  /**
   * @brief Transfer ownership without relocating the backing storage; existing references follow the
   * destination owner.
   */
  ClassFile(ClassFile&&) noexcept;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ClassFile& operator=(ClassFile&&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ClassFile(const ClassFile&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  ClassFile& operator=(const ClassFile&) = delete;

  /**
   * @brief Read the exact class-file version.
   * @return Major and minor version values.
   */
  [[nodiscard]] ClassVersion version() const noexcept;
  /**
   * @brief Read the class access mask.
   * @return The original or programmatic JVM access bits.
   */
  [[nodiscard]] std::uint16_t access_flags() const noexcept;
  /**
   * @brief Read the identity constant reference.
   * @return A Class constant naming this declaration.
   */
  [[nodiscard]] CpIndex this_class() const noexcept;
  /**
   * @brief Read the direct superclass reference.
   * @return A Class constant, or zero for Object and module-info.
   */
  [[nodiscard]] CpIndex super_class() const noexcept;
  /**
   * @brief Read the model's parsing provenance.
   * @return Clean, recovered, opaque-essential, or programmatic provenance; this is not verifier
   *   certification.
   */
  [[nodiscard]] ClassValidity validity() const noexcept;
  /**
   * @brief Read the declaration identity stamp.
   * @return A stamp for class identity, interfaces, member declarations, and pool compaction.
   */
  [[nodiscard]] std::uint64_t declaration_revision() const noexcept;
  /**
   * @brief Access the class-owned constant pool.
   * @return A borrowed pool reference valid until class storage destruction.
   */
  [[nodiscard]] ConstantPool& constant_pool() noexcept;
  /**
   * @brief Access the class-owned constant pool.
   * @return A borrowed pool reference valid until class storage destruction.
   */
  [[nodiscard]] const ConstantPool& constant_pool() const noexcept;

  /**
   * @brief Inspect direct interfaces in declaration order.
   * @return A borrowed span invalidated by adding interfaces or destroying the class.
   *
   * Adding or erasing entries can invalidate views into the corresponding
   * list. Views of source bytes remain valid until class destruction.
   */
  [[nodiscard]] std::span<const CpIndex> interfaces() const noexcept;
  /**
   * @brief Inspect fields in declaration order.
   * @return A borrowed span invalidated by adding or erasing fields or destroying the class.
   */
  [[nodiscard]] std::span<const Field> fields() const noexcept;
  /**
   * @brief Inspect methods in declaration order.
   * @return A borrowed span invalidated by adding or erasing methods or destroying the class.
   */
  [[nodiscard]] std::span<const Method> methods() const noexcept;
  /**
   * @brief Inspect ordered class attributes.
   * @return A borrowed span invalidated by adding attributes or destroying the class.
   *   Symbolic dynamic edits may add a BootstrapMethods attribute or grow its row storage.
   */
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept;
  /**
   * @brief Inspect the retained original input.
   * @return A borrowed byte span valid until class storage destruction; empty for programmatic
   *   classes.
   */
  [[nodiscard]] std::span<const std::byte> source_bytes() const noexcept;
  /**
   * @brief Resolve a checked range in the retained source.
   * @param slice Byte offset and length within the owner's original source buffer.
   * @return A borrowed span, or nullopt when the range exceeds the source buffer.
   */
  [[nodiscard]] std::optional<std::span<const std::byte>> source_slice(SourceSlice slice) const noexcept;

  /**
   * @brief Find the first field with an exact JVM name and descriptor.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param descriptor JVM field or method descriptor in modified UTF-8.
   * @return Its current table index, or nullopt; later insertion/erasure can change indices.
   */
  [[nodiscard]] std::optional<std::size_t> find_field(std::string_view name, std::string_view descriptor) const noexcept;
  /**
   * @brief Find the first method with an exact JVM name and descriptor.
   * @param name Borrowed JVM name in modified UTF-8.
   * @param descriptor JVM field or method descriptor in modified UTF-8.
   * @return Its current table index, or nullopt; later insertion/erasure can change indices.
   */
  [[nodiscard]] std::optional<std::size_t> find_method(std::string_view name, std::string_view descriptor) const noexcept;

  /**
   * @brief Iterate class-aware instructions in method order.
   * @param method_index Zero-based method-table index.
   * @return A sized borrowed range, empty for an invalid index or absent/opaque Code.
   *   Reacquire it after editing the method or changing the method table.
   */
  [[nodiscard]] InstructionViewRange instructions(std::size_t method_index) const noexcept;
  /**
   * @brief Look up an instruction range and report whether decoded Code is available.
   * @param method_index Zero-based method-table index.
   * @return A range (possibly empty), InvalidMethod for an out-of-bounds index,
   *   or NoCode for absent or opaque Code.
   */
  [[nodiscard]] std::expected<InstructionViewRange, CodeEditError> checked_instructions(std::size_t method_index) const noexcept;
  /**
   * @brief Look up a fresh class-aware instruction snapshot by stable handle.
   * @param location Current method-table index and instruction handle.
   * @return The view, or InvalidMethod, NoCode, or InvalidInstruction for a stale/foreign handle.
   */
  [[nodiscard]] std::expected<InstructionView, CodeEditError> instruction(InstructionLocation location) const noexcept;

  /**
   * @brief Find ordinary calls with an exact symbolic owner, name, and descriptor.
   * @param owner Internal JVM owner name or array descriptor in modified UTF-8.
   * @param name Target method name in modified UTF-8.
   * @param descriptor Target method descriptor in modified UTF-8.
   * @param method_index Restrict the search to this method, or nullopt to search the class.
   * @return Locations in method and instruction order, or an invalid method or operand error.
   *   Methods without decoded code are skipped. Owner comparison uses the reference as encoded.
   */
  [[nodiscard]] std::expected<std::vector<InstructionLocation>, CodeEditError> find_invocations(
      std::string_view owner, std::string_view name, std::string_view descriptor,
      std::optional<std::size_t> method_index = std::nullopt) const;
  /**
   * @brief Find field accesses with an exact symbolic owner, name, and descriptor.
   * @param owner Internal JVM owner name in modified UTF-8.
   * @param name Target field name in modified UTF-8.
   * @param descriptor Target field descriptor in modified UTF-8.
   * @param method_index Restrict the search to this method, or nullopt to search the class.
   * @return Locations in method and instruction order, or an invalid method or operand error.
   *   Methods without decoded code are skipped. Owner comparison uses the reference as encoded.
   */
  [[nodiscard]] std::expected<std::vector<InstructionLocation>, CodeEditError> find_field_accesses(
      std::string_view owner, std::string_view name, std::string_view descriptor,
      std::optional<std::size_t> method_index = std::nullopt) const;
  /**
   * @brief Resolve an instruction's symbolic operand and any reachable bootstrap arguments.
   * @param location Current method index and instruction handle.
   * @return A symbolic operand, or InvalidMethod, NoCode, InvalidInstruction, or InvalidOperand
   *   on invalid input. WrongOperandForm identifies an instruction outside the symbolic forms.
   */
  [[nodiscard]] std::expected<SymbolicInstruction, CodeEditError> inspect_instruction(InstructionLocation location) const;

  /**
   * @brief Append a validated direct-interface Class reference.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return Success or InvalidReference/LimitExceeded; updates the declaration revision on success.
   */
  [[nodiscard]] std::expected<void, ModelError> add_interface(CpIndex index);
  /// Add a direct interface by JVM internal name.
  /// @tparam Name Text convertible to string_view.
  /// @param name Interface name in modified UTF-8.
  /// @return Success or a declaration error.
  template<std::convertible_to<std::string_view> Name>
  [[nodiscard]] std::expected<void, ModelError> add_interface(Name&& name) {
    return add_named_interface(std::forward<Name>(name));
  }
  /**
   * @brief Append a field and transfer its attribute payloads into class storage.
   * @param flags JVM access bit mask for the declaration.
   * @param name UTF-8 constant-pool reference for the name.
   * @param descriptor UTF-8 constant-pool reference for the descriptor.
   * @param attributes Ordered attributes transferred into the declaration or supplied metadata.
   * @return The new table index, or a validation/limit error.
   */
  [[nodiscard]] std::expected<std::size_t, ModelError> add_field(
      std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes = {});
  /// Add a field, resolving its name and descriptor in the owning class.
  /// @tparam Name Text convertible to string_view.
  /// @param flags JVM access flags for the field.
  /// @param name Field name in modified UTF-8.
  /// @param descriptor JVM field descriptor.
  /// @param attributes Attributes transferred to the declaration.
  /// @return The new field index or a declaration error.
  template<std::convertible_to<std::string_view> Name>
  [[nodiscard]] std::expected<std::size_t, ModelError> add_field(
      std::uint16_t flags, Name&& name, std::string_view descriptor, std::vector<Attribute> attributes = {}) {
    return add_named_field(flags, std::forward<Name>(name), descriptor, std::move(attributes));
  }
  /**
   * @brief Append a method and transfer its attribute payloads into class storage.
   * @param flags JVM access bit mask for the declaration.
   * @param name UTF-8 constant-pool reference for the name.
   * @param descriptor UTF-8 constant-pool reference for the descriptor.
   * @param attributes Ordered attributes transferred into the declaration or supplied metadata.
   * @return The new table index, or a validation/limit error.
   */
  [[nodiscard]] std::expected<std::size_t, ModelError> add_method(
      std::uint16_t flags, CpIndex name, CpIndex descriptor, std::vector<Attribute> attributes = {});
  /// Add a method, resolving its name and descriptor in the owning class.
  /// @tparam Name Text convertible to string_view.
  /// @param flags JVM access flags for the method.
  /// @param name Method name in modified UTF-8.
  /// @param descriptor JVM method descriptor.
  /// @param attributes Attributes transferred to the declaration.
  /// @return An editor for the new method or a declaration error.
  template<std::convertible_to<std::string_view> Name>
  [[nodiscard]] std::expected<MethodEditor, ModelError> add_method(
      std::uint16_t flags, Name&& name, std::string_view descriptor, std::vector<Attribute> attributes = {}) {
    return add_named_method(flags, std::forward<Name>(name), descriptor, std::move(attributes));
  }
  /**
   * @brief Erase one field in declaration order.
   *
   * The caller must update references to the removed declaration. Existing borrowed views into the
   * corresponding declaration table are invalidated.
   * @param index Zero-based index in the corresponding table or slot array.
   * @return Success or InvalidField; later indices shift and constant-pool slots remain intact.
   */
  [[nodiscard]] std::expected<void, ModelError> erase_field(std::size_t index);
  /**
   * @brief Erase one method in declaration order.
   *
   * The caller must update references to the removed declaration. Existing borrowed views into the
   * corresponding declaration table are invalidated.
   * @param index Zero-based index in the corresponding table or slot array.
   * @return Success or InvalidMethod; later indices shift and constant-pool slots remain intact.
   */
  [[nodiscard]] std::expected<void, ModelError> erase_method(std::size_t index);
  /**
   * @brief Append a class attribute with a valid UTF-8 name reference.
   * @param value Attribute transferred into the class attribute table.
   * @return Success or an invalid-reference/limit error.
   */
  [[nodiscard]] std::expected<void, ModelError> add_attribute(Attribute value);
  /**
   * @brief Attach an empty decoded Code attribute to a method.
   * @param method Zero-based method-table index to receive Code.
   * @param name UTF-8 constant-pool reference for the name.
   * @return A borrowed CodeAttribute reference, or an invalid method/name/duplicate-Code error.
   */
  [[nodiscard]] std::expected<std::reference_wrapper<CodeAttribute>, ModelError>
  add_code(std::size_t method, CpIndex name);
  /// Attach decoded Code, resolving the attribute name automatically.
  /// @param method Zero-based method-table index.
  /// @return The new body or a declaration error.
  [[nodiscard]] std::expected<std::reference_wrapper<CodeAttribute>, ModelError> add_code(std::size_t method);
  /**
   * @brief Borrow an editor for a method table index.
   * @param index Zero-based index in the corresponding table or slot array.
   * @return An editor, or nullopt for an out-of-range index.
   */
  [[nodiscard]] std::optional<MethodEditor> edit_method(std::size_t index) noexcept;
  /// Open the first method with the given JVM name and descriptor.
  /// @param name Method name in modified UTF-8.
  /// @param descriptor JVM method descriptor in modified UTF-8.
  /// @return A borrowed editor, or nullopt when the method is absent.
  [[nodiscard]] std::optional<MethodEditor> edit_method(std::string_view name, std::string_view descriptor) noexcept;
  /**
   * @brief Borrow mutable decoded Code from a method.
   * @param method Zero-based method-table index to inspect.
   * @return The CodeAttribute pointer, or nullptr for an invalid index or absent/opaque Code.
   */
  [[nodiscard]] CodeAttribute* edit_code(std::size_t method) noexcept;

  /**
   * @brief Reclaim unused constants and bootstrap rows directly in the class model.
   * @return Number of reclaimed pool slots, or an error that leaves the model unchanged.
   *
   * Instruction handles and labels remain valid. Reacquire pool indices, copied operands,
   * and borrowed constant or attribute payloads after a successful compaction.
   * An opaque payload whose references cannot be determined prevents compaction.
   */
  [[nodiscard]] std::expected<std::size_t, WriteError> compact_constant_pool();

private:
  friend class MethodEditor;
  friend class RawAttribute;
  /// @param name Internal interface name to intern.
  /// @return Success or a model error.
  std::expected<void, ModelError> add_named_interface(std::string_view name);
  /// @param flags Field access mask.
  /// @param name Field name to intern.
  /// @param descriptor Field descriptor to intern.
  /// @param attributes Attributes to transfer to the field.
  /// @return The field index or a model error.
  std::expected<std::size_t, ModelError> add_named_field(
      std::uint16_t flags, std::string_view name, std::string_view descriptor, std::vector<Attribute> attributes);
  /// @param flags Method access mask.
  /// @param name Method name to intern.
  /// @param descriptor Method descriptor to intern.
  /// @param attributes Attributes to transfer to the method.
  /// @return An editor for the new method or a model error.
  std::expected<MethodEditor, ModelError> add_named_method(
      std::uint16_t flags, std::string_view name, std::string_view descriptor, std::vector<Attribute> attributes);
  /// @param storage Class owning the method and pool.
  /// @param method Concrete method to receive the Code attribute.
  /// @param name Pool index of the Code attribute name.
  /// @return The new code body or a model error.
  static std::expected<std::reference_wrapper<CodeAttribute>, ModelError> attach_code(
      detail::ClassStorage& storage, Method& method, CpIndex name);
  friend class detail::ClassReader;
  friend class detail::SsaCommit;
  /**
   * @brief Take ownership of already validated class storage.
   * @param storage Owned implementation storage transferred into this object.
   */
  explicit ClassFile(std::unique_ptr<detail::ClassStorage> storage) noexcept;
  std::unique_ptr<detail::ClassStorage> storage; ///< Owned backing storage for this object and its borrowed views.
};

} // namespace cafe
