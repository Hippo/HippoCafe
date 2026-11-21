/**
 * @file
 * @brief Stable instruction handles, operand storage, and bytecode editing.
 * @ingroup bytecode
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <type_traits>

#include "cafe/bytecode/opcode.hpp"
#include "cafe/ids.hpp"

namespace cafe::detail {
struct BytecodeAccess;
}

namespace cafe {

/**
 * @brief Instruction, label, and switch editing failures.
 * @ingroup bytecode
 */
enum class CodeEditError {
  WrongOperandForm, ///< Opcode or stored operand does not support the requested form.
  InvalidOperand, ///< Operand is outside the allowed range or structurally invalid.
  InvalidInstruction, ///< Instruction opcode or handle is invalid for the operation.
  InvalidLabel, ///< Label is foreign, stale, or structurally invalid.
  UnboundLabel, ///< Label has no instruction/end binding.
  LabeledInstruction, ///< Deletion would orphan a bound label; retarget it first.
  InvalidSwitch, ///< Switch handle or payload is invalid.
  HandleLimit, ///< Handle slots, generation space, or revision space is exhausted.
  NoCode, ///< The method has no decoded Code attribute.
  UnsupportedClassVersion, ///< The declared version does not support the symbolic operand.
  ConstantPoolLimit, ///< The symbolic edit would exceed the class's constant-pool capacity.
  BootstrapLimit, ///< The symbolic edit would exceed a BootstrapMethods count limit.
  InvalidMethod, ///< The method-table index is out of range.
  StaleMatch, ///< The matched instructions were modified, removed, separated, or belong to another method.
};

/**
 * @brief Preferred wire form retained alongside a logical instruction.
 * @ingroup bytecode
 */
enum class EncodingHint : std::uint8_t {
  Automatic, ///< Let layout choose the appropriate encoding.
  Implicit, ///< Prefer the opcode-embedded local-variable form.
  Explicit, ///< Prefer an explicit non-wide operand form.
  Wide, ///< Prefer a wide operand encoding where legal.
};

/**
 * @brief Primitive newarray atype values defined by the JVM format.
 * @ingroup bytecode
 */
enum class ArrayType : std::uint8_t {
  Boolean = 4, ///< Primitive Java boolean array element type.
  Char = 5, ///< Primitive Java char array element type.
  Float = 6, ///< Primitive Java float array element type.
  Double = 7, ///< Primitive Java double array element type.
  Byte = 8, ///< Primitive Java byte array element type.
  Short = 9, ///< Primitive Java short array element type.
  Int = 10, ///< Primitive Java int array element type.
  Long = 11, ///< Primitive Java long array element type.
};

/**
 * @brief Branch builder retaining a scoped destination label until insertion validates ownership.
 * @ingroup bytecode
 */
struct Branch {
  Opcode opcode; ///< Logical branch opcode.
  LabelId target; ///< Destination label scoped to the owning method.
  EncodingHint encoding_hint = EncodingHint::Automatic; ///< Preferred original wire-encoding form.
};

/**
 * @brief Signed increment and local-variable slot for an iinc instruction.
 * @ingroup bytecode
 */
struct Increment {
  std::uint16_t local; ///< Zero-based local-variable slot.
  std::int16_t amount; ///< Signed amount added by iinc.
};

class MethodCode;

/**
 * @brief Eight-byte trivially copyable logical instruction.
 *
 * Factories validate operand forms; class-specific constant and type validity is checked during
 * writing or analysis. Branch and switch operands must be interpreted through their owning
 * MethodCode.
 * @ingroup bytecode
 */
struct Instruction {
  /**
   * @brief Create a nop by default, or pack an internally validated instruction payload.
   */
  Instruction() = default;

  /**
   * @brief Do nothing.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction nop() noexcept {
    return {};
  }
  /**
   * @brief Discard a category-one stack value.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction pop() noexcept {
    return Instruction(Opcode::Pop, 0);
  }
  /**
   * @brief Discard two stack slots in a legal JVM category grouping.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction pop2() noexcept {
    return Instruction(Opcode::Pop2, 0);
  }
  /**
   * @brief Duplicate a category-one stack value.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction dup() noexcept {
    return Instruction(Opcode::Dup, 0);
  }
  /**
   * @brief Return from a void method.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction return_void() noexcept {
    return Instruction(Opcode::Return, 0);
  }
  /**
   * @brief Push a signed byte as an int.
   * @param value Signed integer immediate to encode.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction bipush(std::int8_t value) noexcept {
    return Instruction(Opcode::Bipush, static_cast<std::uint32_t>(value));
  }
  /**
   * @brief Push a signed short as an int.
   * @param value Signed integer immediate to encode.
   * @return The corresponding logical instruction; stack legality is checked during analysis.
   */
  [[nodiscard]] static constexpr Instruction sipush(std::int16_t value) noexcept {
    return Instruction(Opcode::Sipush, static_cast<std::uint32_t>(value));
  }
  /**
   * @brief Load an int value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction iload(std::uint16_t index) noexcept {
    return Instruction(Opcode::ILoad, index);
  }
  /**
   * @brief Load a long value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction lload(std::uint16_t index) noexcept {
    return Instruction(Opcode::LLoad, index);
  }
  /**
   * @brief Load a float value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction fload(std::uint16_t index) noexcept {
    return Instruction(Opcode::FLoad, index);
  }
  /**
   * @brief Load a double value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction dload(std::uint16_t index) noexcept {
    return Instruction(Opcode::DLoad, index);
  }
  /**
   * @brief Load a reference value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction aload(std::uint16_t index) noexcept {
    return Instruction(Opcode::ALoad, index);
  }
  /**
   * @brief Store an int value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction istore(std::uint16_t index) noexcept {
    return Instruction(Opcode::IStore, index);
  }
  /**
   * @brief Store a long value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction lstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::LStore, index);
  }
  /**
   * @brief Store a float value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction fstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::FStore, index);
  }
  /**
   * @brief Store a double value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction dstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::DStore, index);
  }
  /**
   * @brief Store a reference value using a local-variable slot.
   * @param index Zero-based local-variable slot index.
   * @return The logical instruction; the writer selects implicit, explicit, or wide encoding.
   */
  [[nodiscard]] static constexpr Instruction astore(std::uint16_t index) noexcept {
    return Instruction(Opcode::AStore, index);
  }

  /**
   * @brief Build an operand-free logical instruction.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @return An instruction, or WrongOperandForm if the opcode requires operands or is illegal.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> simple(Opcode opcode);
  /**
   * @brief Build a signed byte or short immediate instruction.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param value Signed integer immediate to encode.
   * @return An instruction, or an operand-form/range error.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> immediate(Opcode opcode, std::int32_t value);
  /**
   * @brief Build a logical local-variable instruction.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param index Zero-based local-variable slot index.
   * @return An instruction, or WrongOperandForm; encoding width is selected during
   *   writing.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> local(Opcode opcode, std::uint16_t index);
  /**
   * @brief Build an iinc instruction.
   * @param index Zero-based local-variable slot index.
   * @param amount Signed iinc increment.
   * @return The logical local increment; the writer selects wide encoding when needed.
   */
  [[nodiscard]] static Instruction increment(std::uint16_t index, std::int16_t amount);
  /**
   * @brief Build a constant-pool operand instruction.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @return An instruction, or a form/index error; pool tag compatibility is checked later.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> constant(Opcode opcode, CpIndex index);
  /**
   * @brief Build an invokeinterface instruction.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @param count Nonzero receiver-plus-argument slot count encoded by invokeinterface.
   * @return An instruction, or an invalid index/count error; count includes the receiver and
   *   argument slots.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> invoke_interface(CpIndex index, std::uint8_t count);
  /**
   * @brief Build a multianewarray instruction.
   * @param index One-based constant-pool reference; the required tag is determined by this
   *   operation.
   * @param dimensions Nonzero number of array dimensions to allocate.
   * @return An instruction, or an invalid index/dimension error; array rank is checked against the
   *   pool later.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> multi_array(CpIndex index, std::uint8_t dimensions);
  /**
   * @brief Build a primitive newarray instruction.
   * @param type Primitive array element type.
   * @return An instruction, or InvalidOperand for an invalid primitive atype.
   */
  [[nodiscard]] static std::expected<Instruction, CodeEditError> new_array(ArrayType type);
  /**
   * @brief Build a scoped branch descriptor.
   * @param opcode Opcode whose operand form or metadata is requested.
   * @param target Label handle scoped to the supplied method.
   * @return A branch builder, or WrongOperandForm for an incompatible opcode. MethodCode checks
   *   label ownership during insertion; writing also requires the label to be bound.
   */
  [[nodiscard]] static std::expected<Branch, CodeEditError> branch(Opcode opcode, LabelId target);

  /**
   * @brief Read the logical operation.
   * @return The logical opcode, which can differ from its preserved wire encoding.
   */
  [[nodiscard]] constexpr Opcode opcode() const {
    return operation;
  }
  /**
   * @brief Read the preferred original encoding form.
   * @return The encoding hint used by preservation-aware serialization.
   */
  [[nodiscard]] constexpr EncodingHint encoding_hint() const {
    return hint;
  }
  /**
   * @brief Inspect a signed immediate operand.
   * @return The value, or nullopt for a different operand form.
   */
  [[nodiscard]] std::optional<std::int32_t> immediate_value() const;
  /**
   * @brief Inspect a local load/store/ret slot.
   * @return The slot index, or nullopt for a different operand form; use increment_value() for iinc.
   */
  [[nodiscard]] std::optional<std::uint16_t> local_index() const;
  /**
   * @brief Inspect the iinc operand pair.
   * @return The local and signed amount, or nullopt for a different opcode.
   */
  [[nodiscard]] std::optional<Increment> increment_value() const;
  /**
   * @brief Inspect a constant-pool operand.
   * @return The index, or nullopt for an instruction without a pool operand.
   */
  [[nodiscard]] std::optional<CpIndex> constant_index() const;
  /**
   * @brief Inspect the invokeinterface slot count.
   * @return The encoded count, or nullopt for a different opcode.
   */
  [[nodiscard]] std::optional<std::uint8_t> interface_count() const;
  /**
   * @brief Inspect the multianewarray allocation rank.
   * @return The dimension count, or nullopt for a different opcode.
   */
  [[nodiscard]] std::optional<std::uint8_t> dimensions() const;
  /**
   * @brief Inspect the newarray primitive element type.
   * @return The type, or nullopt for a different opcode.
   */
  [[nodiscard]] std::optional<ArrayType> array_type() const;

  friend bool operator==(const Instruction&, const Instruction&) = default;

private:
  friend class MethodCode;
  friend struct detail::BytecodeAccess;

  /**
   * @brief Create a nop by default, or pack an internally validated instruction payload.
   * @param operation Logical opcode stored in the compact instruction.
   * @param operand_bits Primary packed operand bits interpreted according to the opcode.
   * @param secondary Secondary packed operand, such as iinc amount or invocation count.
   * @param hint Preferred wire-encoding form.
   */
  constexpr Instruction(Opcode operation, std::uint32_t operand_bits, std::uint16_t secondary = 0,
                        EncodingHint hint = EncodingHint::Automatic) :
      operand_bits(operand_bits), secondary(secondary), operation(operation), hint(hint) {
  }

  std::uint32_t operand_bits = 0; ///< Primary packed instruction operand; interpretation follows the opcode.
  std::uint16_t secondary = 0; ///< Secondary packed operand for increment/count/dimension forms.
  Opcode operation = Opcode::Nop; ///< Logical opcode stored in the compact instruction.
  EncodingHint hint = EncodingHint::Automatic; ///< Retained encoding preference.
};

static_assert(sizeof(Instruction) == 8);
static_assert(std::is_trivially_copyable_v<Instruction>);

} // namespace cafe
