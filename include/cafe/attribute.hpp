/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup model
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include <cafe/attribute_kind.hpp>
#include <cafe/bytecode/code.hpp>
#include <cafe/ids.hpp>

namespace cafe {

class ClassFile;
class CodeAttribute;

namespace detail {
class ClassReader;
class ClassWriter;
class SymbolicEdit;
} // namespace detail

/**
 * @brief Half-open byte range in the owning ClassFile source buffer. Offsets and lengths are byte
 * counts, not pointers.
 * @ingroup model
 */
struct SourceSlice {
  std::uint32_t offset; ///< Absolute byte offset in the owning class source buffer.
  std::uint32_t length; ///< Length of the byte range.
  friend constexpr bool operator==(SourceSlice, SourceSlice) = default;
};

/**
 * @brief Opaque attribute bytes, either borrowed from a class source slice or independently owned.
 *
 * Its payload is preserved without claiming semantic editing support.
 * @ingroup model
 */
class RawAttribute {
public:
  /**
   * @brief Resolve the opaque payload against its owning class.
   * @param owner Class owning the method, constants, or source slices being inspected.
   * @return A borrowed byte span, or nullopt if a source slice is out of bounds.
   *
   * Pass the class that owns this attribute. The returned view is valid for
   * the lifetime of its source or of this owned payload.
   */
  [[nodiscard]] std::optional<std::span<const std::byte>> bytes(const ClassFile& owner) const noexcept;
  /**
   * @brief Inspect whether the payload borrows original source bytes.
   * @return A borrowed slice pointer, or nullptr for independently owned bytes.
   */
  [[nodiscard]] const SourceSlice* source_slice() const noexcept;

private:
  friend class Attribute;
  friend class detail::ClassReader;
  /**
   * @brief Retain a source slice or take ownership of an independent opaque payload.
   * @param slice Byte offset and length within the owner's original source buffer.
   */
  explicit RawAttribute(SourceSlice slice) noexcept;
  /**
   * @brief Retain a source slice or take ownership of an independent opaque payload.
   * @param bytes Owned byte buffer transferred into this object.
   */
  explicit RawAttribute(std::vector<std::byte> bytes) noexcept;

  std::variant<SourceSlice, std::vector<std::byte>> storage; ///< Source slice into the owning class or independently owned byte vector.
};

/**
 * @brief Typed SourceFile attribute referencing the source filename.
 * @ingroup model
 */
struct SourceFile {
  CpIndex file_name; ///< UTF-8 constant-pool reference for the source filename.
};
/**
 * @brief Typed Signature attribute containing a generic-signature string reference.
 * @ingroup model
 */
struct Signature {
  CpIndex signature; ///< UTF-8 constant-pool reference for the generic signature.
};
/**
 * @brief Typed field ConstantValue attribute referencing its initializer constant.
 * @ingroup model
 */
struct ConstantValue {
  CpIndex constant; ///< Constant-pool reference for the field initializer.
};
/**
 * @brief Zero-length Synthetic marker attribute.
 * @ingroup model
 */
struct Synthetic {};
/**
 * @brief Zero-length Deprecated marker attribute.
 * @ingroup model
 */
struct Deprecated {};

/**
 * @brief Typed Exceptions attribute containing declared exception class references.
 * @ingroup model
 */
struct Exceptions {
  std::pmr::vector<CpIndex> types; ///< Declared exception Class references in attribute order.
};

/**
 * @brief Bootstrap method handle and ordered static argument references.
 * @ingroup model
 */
struct BootstrapMethod {
  CpIndex method_handle; ///< MethodHandle constant selecting the bootstrap invocation.
  std::pmr::vector<CpIndex> arguments; ///< Ordered bootstrap argument constant-pool references.
};

/**
 * @brief Typed BootstrapMethods attribute containing the bootstrap table.
 * @ingroup model
 */
struct BootstrapMethods {
  std::pmr::vector<BootstrapMethod> methods; ///< Ordered bootstrap records.
};

/**
 * @brief Undecodable but bounded original Code body retained under explicit reader policy.
 *
 * Analysis is unavailable and writing requires an explicit opaque-Code opt-in.
 * @ingroup model
 */
struct OpaqueCode {
  SourceSlice body; ///< Original attribute-body source slice.
};

/**
 * @brief Move-constructible tagged attribute payload with a constant-pool name.
 *
 * Ordered attribute lists own their entries; allocator-bound entries deliberately prohibit
 * assignment.
 * @ingroup model
 */
class Attribute {
public:
  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param value Typed attribute payload to store.
   */
  Attribute(CpIndex name, SourceFile value) noexcept;
  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param value Typed attribute payload to store.
   */
  Attribute(CpIndex name, Signature value) noexcept;
  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param value Typed attribute payload to store.
   */
  Attribute(CpIndex name, ConstantValue value) noexcept;
  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param value Typed attribute payload to store.
   */
  Attribute(CpIndex name, Synthetic value) noexcept;
  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param value Typed attribute payload to store.
   */
  Attribute(CpIndex name, Deprecated value) noexcept;

  /**
   * @brief Create an opaque attribute owning the supplied bytes.
   * @param name UTF-8 constant-pool reference for the name.
   * @param bytes Owned byte buffer transferred into this object.
   * @return An attribute retaining the payload without semantic validation.
   */
  [[nodiscard]] static Attribute raw(CpIndex name, std::vector<std::byte> bytes) noexcept;
  /**
   * @brief Copy declared exception class references into an attribute.
   * @param name UTF-8 constant-pool reference for the name.
   * @param types Ordered exception Class references to copy.
   * @return An owning Exceptions attribute.
   */
  [[nodiscard]] static Attribute exceptions(CpIndex name, std::span<const CpIndex> types);
  /**
   * @brief Copy bootstrap handles and arguments into an attribute.
   * @param name UTF-8 constant-pool reference for the name.
   * @param methods Ordered bootstrap records to copy, including their arguments.
   * @return An owning BootstrapMethods attribute.
   */
  [[nodiscard]] static Attribute bootstrap_methods(CpIndex name, std::span<const BootstrapMethod> methods);

  /**
   * @brief Move the attribute and its payload. Reacquire pointers returned by get() from the
   * destination; inline payload alternatives move with the Attribute object.
   */
  Attribute(Attribute&&) noexcept = default;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Attribute& operator=(Attribute&&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Attribute(const Attribute&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  Attribute& operator=(const Attribute&) = delete;

  /**
   * @brief Read the attribute's name reference.
   * @return The UTF-8 constant-pool index for the attribute name.
   */
  [[nodiscard]] CpIndex name_index() const noexcept {
    return name;
  }
  /**
   * @brief Read the payload discriminator.
   * @return The recognized attribute-name kind, or Unknown. A recognized kind may still have a
   *   RawAttribute payload; use get() to inspect its representation.
   */
  [[nodiscard]] AttributeKind kind() const noexcept {
    return attribute_kind;
  }

  /**
   * @brief Inspect the stored alternative without changing the attribute.
   * @return A borrowed payload pointer, or nullptr when Value is not the active alternative.
   */
  template<typename Value>
  [[nodiscard]] const Value* get() const noexcept {
    if constexpr (std::is_same_v<Value, CodeAttribute>) {
      const auto* code = std::get_if<CodeOwner>(&payload);
      return code ? code->get() : nullptr;
    } else {
      return std::get_if<Value>(&payload);
    }
  }

private:
  friend class ClassFile;
  friend class MethodEditor;
  friend class detail::ClassReader;
  friend class detail::ClassWriter;
  friend class detail::SymbolicEdit;

  /**
   * @brief Destroys a CodeAttribute through the same PMR resource that allocated it.
   * @ingroup model
   */
  struct CodeDeleter {
    /**
     * @brief Destroy and deallocate the payload through its original PMR resource.
     * @param value Code payload to destroy through its allocation resource.
     */
    void operator()(CodeAttribute* value) const noexcept;
  };

  /**
   * @brief Unique PMR-aware ownership of a decoded Code attribute.
   */
  using CodeOwner = std::unique_ptr<CodeAttribute, CodeDeleter>;
  /**
   * @brief Closed set of typed and opaque attribute payload alternatives.
   */
  using Payload = std::variant<RawAttribute, SourceFile, Signature, ConstantValue, Synthetic,
                               Deprecated, Exceptions, BootstrapMethods, CodeOwner, OpaqueCode>;

  /**
   * @brief Construct an attribute by transferring its typed or opaque payload.
   * @param name UTF-8 constant-pool reference for the name.
   * @param kind Payload or error discriminator.
   * @param payload Owned attribute alternative to install.
   */
  Attribute(CpIndex name, AttributeKind kind, Payload payload) noexcept;
  /**
   * @brief Allocate an empty Code attribute using the class resource.
   * @param name UTF-8 constant-pool reference for the name.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   * @return An attribute owning its allocator-bound Code payload.
   */
  static Attribute new_code(CpIndex name, std::pmr::memory_resource* resource);
  /**
   * @brief Access the Code alternative for an authorized editor.
   * @return A borrowed pointer, or nullptr when the payload is not decoded Code.
   */
  [[nodiscard]] CodeAttribute* mutable_code() noexcept;

  CpIndex name; ///< UTF-8 constant-pool reference for the declaration or attribute name.
  AttributeKind attribute_kind; ///< Discriminator for the stored attribute alternative.
  Payload payload; ///< Owned typed or opaque attribute alternative.
};

/**
 * @brief Exception-table entry using method-local labels. The protected interval includes start
 * and excludes end; catch_type zero catches every throwable.
 * @ingroup model
 */
struct ExceptionHandler {
  LabelId start; ///< Inclusive protected-range boundary.
  LabelId end; ///< Exclusive protected-range boundary, possibly method end.
  LabelId handler; ///< Handler entry boundary; must identify an instruction.
  CpIndex catch_type; ///< Exception Class reference; zero denotes a catch-all handler.
  friend bool operator==(const ExceptionHandler&, const ExceptionHandler&) = default;
};

/**
 * @brief Owns a method's instructions, exception table, and derived verifier/debug metadata.
 *
 * Editing instructions or handlers requires computed or caller-supplied metadata before emission.
 * The allocator and owning class must outlive borrowed views; concurrent mutation is unsupported.
 * @ingroup model
 */
class CodeAttribute {
public:
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  CodeAttribute(const CodeAttribute&) = delete;
  /**
   * @brief Disabled because copying or assignment would violate this owner's storage/lifetime
   * contract.
   */
  CodeAttribute& operator=(const CodeAttribute&) = delete;

  /**
   * @brief Access the owned logical instruction sequence.
   * @return A borrowed MethodCode reference; edits invalidate derived metadata.
   */
  [[nodiscard]] MethodCode& code() noexcept {
    return instructions;
  }
  /**
   * @brief Access the owned logical instruction sequence.
   * @return A borrowed MethodCode reference; edits invalidate derived metadata.
   */
  [[nodiscard]] const MethodCode& code() const noexcept {
    return instructions;
  }
  /**
   * @brief Read the stored stack limit.
   * @return The declared or caller-supplied maximum stack size in JVM slots, not a recomputation.
   */
  [[nodiscard]] std::uint16_t max_stack() const noexcept {
    return stack_limit;
  }
  /**
   * @brief Read the stored local limit.
   * @return The declared or caller-supplied local count in JVM slots, not a recomputation.
   */
  [[nodiscard]] std::uint16_t max_locals() const noexcept {
    return locals_limit;
  }
  /**
   * @brief Inspect exception handlers in table order.
   * @return A borrowed span invalidated by handler or metadata replacement.
   */
  [[nodiscard]] std::span<const ExceptionHandler> exception_handlers() const noexcept {
    return handlers;
  }
  /**
   * @brief Inspect nested Code attributes in order.
   * @return A borrowed span invalidated by metadata replacement.
   */
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept {
    return nested_attributes;
  }

  /**
   * @brief Replace exception handlers without replacing nested attributes.
   *
   * Unchanged tables are a no-op. A changed table requires a computing writer or fresh supplied
   * metadata before emission.
   * @param new_handlers Replacement exception table using this method's labels.
   *
   * Replaces the exception table independently of the nested attributes.
   * Use a computing writer or supply new metadata before writing the edit.
   */
  void set_exception_handlers(std::span<const ExceptionHandler> new_handlers);

  /**
   * @brief Replace all derived metadata and stamp it for the current code revision and encoding
   * policy.
   *
   * Supply complete handlers, frames, and offset-bearing debug attributes for the chosen output
   * encoding. Further instruction edits expire the stamp. Use
   * CodeWritePolicy::CallerSuppliedMetadata when writing. Previous handler and nested-attribute
   * views are invalidated.
   * @param max_stack Maximum operand-stack size in JVM slots.
   * @param max_locals Maximum local-variable size in JVM slots.
   * @param handlers Exception-table entries in class-file order using the supplied code's labels.
   * @param attributes Ordered attributes transferred into the declaration or supplied metadata.
   * @param preserve_original_encodings Whether to honor retained wire-encoding hints when laying out
   *   instructions.
   *
   * Replace all derived metadata after editing the code. Supply handlers in
   * terms of this method's labels and complete any required frame/debug
   * attributes for the selected encoding, which must match WriteOptions.
   * Further edits expire the metadata. Views of previous handlers and nested
   * attributes are invalidated.
   */
  void supply_metadata(std::uint16_t max_stack, std::uint16_t max_locals,
                       std::span<const ExceptionHandler> handlers, std::vector<Attribute> attributes = {},
                       bool preserve_original_encodings = true);

private:
  friend class Attribute;
  friend class detail::ClassReader;
  friend class detail::ClassWriter;

  /**
   * @brief Source slices retained for unchanged Code emission and bounded recovery.
   * @ingroup model
   */
  struct OriginalCode {
    SourceSlice body; ///< Original attribute-body source slice.
    SourceSlice code; ///< Original code-array source slice, excluding the Code envelope.
    SourceSlice exceptions; ///< Original exception-table bytes, including the count.
    bool nested_recovered; ///< Whether malformed nested attributes were dropped during reading.
  };

  /**
   * @brief Revision and encoding policy for caller-supplied metadata.
   *
   * Further code edits invalidate the stamp.
   * @ingroup model
   */
  struct SuppliedMetadata {
    std::uint64_t revision; ///< Instruction revision captured when this metadata was supplied.
    bool preserve_original_encodings; ///< Whether layout should honor retained wire-encoding hints.
  };

  /**
   * @brief Original byte offset and instruction identity used to relocate offset-bearing metadata.
   * @ingroup model
   */
  struct OriginalBoundary {
    std::uint16_t offset; ///< Byte offset from the beginning of the original code array.
    InstructionId instruction; ///< Instruction identity associated with this record.
    Instruction original_instruction; ///< Original instruction value used to detect offset-sensitive changes.
  };

  /**
   * @brief Create empty allocator-bound code and metadata storage.
   * @param resource PMR resource that must outlive the allocated object or returned storage.
   */
  explicit CodeAttribute(std::pmr::memory_resource* resource);

  MethodCode instructions; ///< Owned logical instruction sequence.
  std::uint16_t stack_limit = 0; ///< Stored maximum operand-stack size in JVM slots.
  std::uint16_t locals_limit = 0; ///< Stored maximum local-variable count in JVM slots.
  std::pmr::vector<ExceptionHandler> handlers; ///< Ordered exception-table entries using scoped labels.
  std::pmr::vector<Attribute> nested_attributes; ///< Ordered attributes nested inside Code.
  std::pmr::vector<OriginalBoundary> original_boundaries; ///< Original offsets and instruction identities used for metadata relocation.
  std::optional<std::pmr::vector<ExceptionHandler>> original_handlers; ///< Original handler table retained after the first handler edit.
  std::optional<OriginalCode> original; ///< Original wire metadata, absent for newly constructed content.
  std::optional<SuppliedMetadata> supplied_metadata; ///< Caller-supplied revision/encoding stamp, if present.
};

} // namespace cafe
