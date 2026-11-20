/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory_resource>
#include <span>
#include <vector>

#include <cafe/attribute.hpp>
#include <cafe/bytecode/method_instruction.hpp>
#include <cafe/ids.hpp>

namespace cafe {

namespace detail {
struct ClassStorage;
}

/**
 * @brief Method declaration owned by ClassFile. Code is either decoded, opaque, or absent. Adding
 * or erasing methods can invalidate references to this object.
 * @ingroup model
 */
class Method {
public:
  /**
   * @brief Move the declaration and transfer its attribute buffer. References to the source
   * declaration do not retarget to the destination.
   */
  Method(Method&&) noexcept = default;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Method& operator=(Method&&) = delete;

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
  /**
   * @brief Inspect decoded Code.
   * @return A borrowed pointer, or nullptr if Code is absent or opaque.
   */
  [[nodiscard]] const CodeAttribute* code() const noexcept;
  /**
   * @brief Inspect preserved undecodable Code.
   * @return A borrowed pointer, or nullptr if Code is absent or decoded.
   */
  [[nodiscard]] const OpaqueCode* opaque_code() const noexcept;

private:
  friend class ClassFile;
  friend class MethodEditor;
  friend class detail::ClassReader;

  /**
   * @brief Create an allocator-bound method declaration with constant-pool identity.
   * @param flags JVM access bit mask for the declaration.
   * @param name UTF-8 constant-pool reference for the name.
   * @param descriptor UTF-8 constant-pool reference for the descriptor.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  Method(std::uint16_t flags, CpIndex name, CpIndex descriptor, std::pmr::memory_resource* resource) : flags(flags), name(name), descriptor(descriptor), attribute_entries(resource) {
  }

  std::uint16_t flags; ///< JVM access bit mask.
  CpIndex name; ///< UTF-8 constant-pool reference for the declaration or attribute name.
  CpIndex descriptor; ///< UTF-8 constant-pool reference for the JVM descriptor.
  std::pmr::vector<Attribute> attribute_entries; ///< Owned ordered attribute list using the declaration's allocator.
};

/**
 * @brief Borrowed mutable access to one ClassFile method. Acquire a new editor after adding or
 * erasing a method; the owning class must outlive the editor.
 *
 * Symbolic edits validate operand structure and version rules. Frame analysis can check stack
 * usage, and the JVM resolves targets and invokes bootstrap methods at linkage. An allocation
 * failure may propagate as std::bad_alloc and leaves logical code and class entries unchanged.
 * Reacquire borrowed class attribute spans, bootstrap row views, and pool entry pointers after
 * symbolic edits.
 * @ingroup model
 */
class MethodEditor {
public:
  /**
   * @brief Inspect the borrowed declaration.
   * @return The method reference; reacquire the editor after adding or erasing methods.
   */
  [[nodiscard]] const Method& method() const noexcept {
    return *member;
  }
  /**
   * @brief Access decoded Code for editing.
   * @return A borrowed mutable pointer, or nullptr when Code is absent or opaque.
   */
  [[nodiscard]] CodeAttribute* code() const noexcept;

  /**
   * @brief Append an instruction, resolving any symbolic operand against the owning class.
   * @param value Instruction and its operands to copy. Borrowed operands are needed only for this call.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> append(const MethodInstruction& value);
  /**
   * @brief Insert before an instruction, leaving its existing labels attached to it.
   * @param at Existing instruction handle in this method.
   * @param value Instruction and its operands to copy.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_before(InstructionId at, const MethodInstruction& value);
  /**
   * @brief Insert at a bound label and move that label to the new instruction.
   * @param at Bound label in this method; the exclusive end is permitted.
   * @param value Instruction and its operands to copy.
   * @return The new instruction handle, or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<InstructionId, CodeEditError> insert_at(LabelId at, const MethodInstruction& value);
  /**
   * @brief Replace an instruction while retaining its handle and label bindings.
   * @param at Existing instruction handle in this method.
   * @param value Instruction and its operands to copy.
   * @return Success or an error leaving the logical code and class entries unchanged.
   */
  [[nodiscard]] std::expected<void, CodeEditError> replace(InstructionId at, const MethodInstruction& value);
  /**
   * @brief Erase an instruction. Retarget any bound labels before erasing their destination.
   * @param id Existing instruction handle in this method.
   * @return Success or an edit error. Erasure leaves class constant and bootstrap indices stable.
   */
  [[nodiscard]] std::expected<void, CodeEditError> erase(InstructionId id);

private:
  friend class ClassFile;
  /**
   * @brief Borrow mutable access to a live class-owned method.
   * @param storage Owning class storage.
   * @param method Method belonging to the supplied class.
   */
  MethodEditor(detail::ClassStorage& storage, Method& method) noexcept : storage(&storage), member(&method) {
  }

  detail::ClassStorage* storage; ///< Owning class storage, which stays fixed across ClassFile moves.
  Method* member; ///< Borrowed method; invalidated by adding or erasing methods in its class.
};

} // namespace cafe
