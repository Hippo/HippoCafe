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
} // namespace detail

struct SourceSlice {
  std::uint32_t offset;
  std::uint32_t length;
  friend constexpr bool operator==(SourceSlice, SourceSlice) = default;
};

class RawAttribute {
public:
  // Pass the class that owns this attribute. The returned view is valid for
  // the lifetime of its source or of this owned payload.
  [[nodiscard]] std::optional<std::span<const std::byte>> bytes(const ClassFile& owner) const noexcept;
  [[nodiscard]] const SourceSlice* source_slice() const noexcept;

private:
  friend class Attribute;
  friend class detail::ClassReader;
  explicit RawAttribute(SourceSlice slice) noexcept;
  explicit RawAttribute(std::vector<std::byte> bytes) noexcept;

  std::variant<SourceSlice, std::vector<std::byte>> storage;
};

struct SourceFile {
  CpIndex file_name;
};
struct Signature {
  CpIndex signature;
};
struct ConstantValue {
  CpIndex constant;
};
struct Synthetic {};
struct Deprecated {};

struct Exceptions {
  std::pmr::vector<CpIndex> types;
};

struct BootstrapMethod {
  CpIndex method_handle;
  std::pmr::vector<CpIndex> arguments;
};

struct BootstrapMethods {
  std::pmr::vector<BootstrapMethod> methods;
};

struct OpaqueCode {
  SourceSlice body;
};

class Attribute {
public:
  Attribute(CpIndex name, SourceFile value) noexcept;
  Attribute(CpIndex name, Signature value) noexcept;
  Attribute(CpIndex name, ConstantValue value) noexcept;
  Attribute(CpIndex name, Synthetic value) noexcept;
  Attribute(CpIndex name, Deprecated value) noexcept;

  [[nodiscard]] static Attribute raw(CpIndex name, std::vector<std::byte> bytes) noexcept;
  [[nodiscard]] static Attribute exceptions(CpIndex name, std::span<const CpIndex> types);
  [[nodiscard]] static Attribute bootstrap_methods(CpIndex name, std::span<const BootstrapMethod> methods);

  Attribute(Attribute&&) noexcept = default;
  Attribute& operator=(Attribute&&) = delete;
  Attribute(const Attribute&) = delete;
  Attribute& operator=(const Attribute&) = delete;

  [[nodiscard]] CpIndex name_index() const noexcept {
    return name;
  }
  [[nodiscard]] AttributeKind kind() const noexcept {
    return attribute_kind;
  }

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

  struct CodeDeleter {
    void operator()(CodeAttribute* value) const noexcept;
  };

  using CodeOwner = std::unique_ptr<CodeAttribute, CodeDeleter>;
  using Payload = std::variant<RawAttribute, SourceFile, Signature, ConstantValue, Synthetic,
                               Deprecated, Exceptions, BootstrapMethods, CodeOwner, OpaqueCode>;

  Attribute(CpIndex name, AttributeKind kind, Payload payload) noexcept;
  static Attribute new_code(CpIndex name, std::pmr::memory_resource* resource);
  [[nodiscard]] CodeAttribute* mutable_code() noexcept;

  CpIndex name;
  AttributeKind attribute_kind;
  Payload payload;
};

struct ExceptionHandler {
  LabelId start;
  LabelId end;
  LabelId handler;
  CpIndex catch_type;
  friend bool operator==(const ExceptionHandler&, const ExceptionHandler&) = default;
};

class CodeAttribute {
public:
  CodeAttribute(const CodeAttribute&) = delete;
  CodeAttribute& operator=(const CodeAttribute&) = delete;

  [[nodiscard]] MethodCode& code() noexcept {
    return instructions;
  }
  [[nodiscard]] const MethodCode& code() const noexcept {
    return instructions;
  }
  [[nodiscard]] std::uint16_t max_stack() const noexcept {
    return stack_limit;
  }
  [[nodiscard]] std::uint16_t max_locals() const noexcept {
    return locals_limit;
  }
  [[nodiscard]] std::span<const ExceptionHandler> exception_handlers() const noexcept {
    return handlers;
  }
  [[nodiscard]] std::span<const Attribute> attributes() const noexcept {
    return nested_attributes;
  }

  // Replaces the exception table independently of the nested attributes.
  // Use a computing writer or supply new metadata before writing the edit.
  void set_exception_handlers(std::span<const ExceptionHandler> new_handlers);

  // Replace all derived metadata after editing the code. Supply handlers in
  // terms of this method's labels and complete any required frame/debug
  // attributes for the selected encoding, which must match WriteOptions.
  // Further edits expire the metadata. Views of previous handlers and nested
  // attributes are invalidated.
  void supply_metadata(std::uint16_t max_stack, std::uint16_t max_locals,
                       std::span<const ExceptionHandler> handlers, std::vector<Attribute> attributes = {},
                       bool preserve_original_encodings = true);

private:
  friend class Attribute;
  friend class detail::ClassReader;
  friend class detail::ClassWriter;

  struct OriginalCode {
    SourceSlice body;
    SourceSlice code;
    SourceSlice exceptions;
    bool nested_recovered;
  };

  struct SuppliedMetadata {
    std::uint64_t revision;
    bool preserve_original_encodings;
  };

  struct OriginalBoundary {
    std::uint16_t offset;
    InstructionId instruction;
    Instruction original_instruction;
  };

  explicit CodeAttribute(std::pmr::memory_resource* resource);

  MethodCode instructions;
  std::uint16_t stack_limit = 0;
  std::uint16_t locals_limit = 0;
  std::pmr::vector<ExceptionHandler> handlers;
  std::pmr::vector<Attribute> nested_attributes;
  std::pmr::vector<OriginalBoundary> original_boundaries;
  std::optional<std::pmr::vector<ExceptionHandler>> original_handlers;
  std::optional<OriginalCode> original;
  std::optional<SuppliedMetadata> supplied_metadata;
};

} // namespace cafe
