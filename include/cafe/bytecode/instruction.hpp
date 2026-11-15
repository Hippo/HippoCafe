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

enum class CodeEditError {
  WrongOperandForm,
  InvalidOperand,
  InvalidInstruction,
  InvalidLabel,
  UnboundLabel,
  LabeledInstruction,
  InvalidSwitch,
  HandleLimit,
};

enum class EncodingHint : std::uint8_t {
  Automatic,
  Implicit,
  Explicit,
  Wide,
};

enum class ArrayType : std::uint8_t {
  Boolean = 4,
  Char = 5,
  Float = 6,
  Double = 7,
  Byte = 8,
  Short = 9,
  Int = 10,
  Long = 11,
};

struct Branch {
  Opcode opcode;
  LabelId target;
  EncodingHint encoding_hint = EncodingHint::Automatic;
};

struct Increment {
  std::uint16_t local;
  std::int16_t amount;
};

class MethodCode;

struct Instruction {
  Instruction() = default;

  [[nodiscard]] static constexpr Instruction nop() noexcept {
    return {};
  }
  [[nodiscard]] static constexpr Instruction pop() noexcept {
    return Instruction(Opcode::Pop, 0);
  }
  [[nodiscard]] static constexpr Instruction pop2() noexcept {
    return Instruction(Opcode::Pop2, 0);
  }
  [[nodiscard]] static constexpr Instruction dup() noexcept {
    return Instruction(Opcode::Dup, 0);
  }
  [[nodiscard]] static constexpr Instruction return_void() noexcept {
    return Instruction(Opcode::Return, 0);
  }
  [[nodiscard]] static constexpr Instruction bipush(std::int8_t value) noexcept {
    return Instruction(Opcode::Bipush, static_cast<std::uint32_t>(value));
  }
  [[nodiscard]] static constexpr Instruction sipush(std::int16_t value) noexcept {
    return Instruction(Opcode::Sipush, static_cast<std::uint32_t>(value));
  }
  [[nodiscard]] static constexpr Instruction iload(std::uint16_t index) noexcept {
    return Instruction(Opcode::ILoad, index);
  }
  [[nodiscard]] static constexpr Instruction lload(std::uint16_t index) noexcept {
    return Instruction(Opcode::LLoad, index);
  }
  [[nodiscard]] static constexpr Instruction fload(std::uint16_t index) noexcept {
    return Instruction(Opcode::FLoad, index);
  }
  [[nodiscard]] static constexpr Instruction dload(std::uint16_t index) noexcept {
    return Instruction(Opcode::DLoad, index);
  }
  [[nodiscard]] static constexpr Instruction aload(std::uint16_t index) noexcept {
    return Instruction(Opcode::ALoad, index);
  }
  [[nodiscard]] static constexpr Instruction istore(std::uint16_t index) noexcept {
    return Instruction(Opcode::IStore, index);
  }
  [[nodiscard]] static constexpr Instruction lstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::LStore, index);
  }
  [[nodiscard]] static constexpr Instruction fstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::FStore, index);
  }
  [[nodiscard]] static constexpr Instruction dstore(std::uint16_t index) noexcept {
    return Instruction(Opcode::DStore, index);
  }
  [[nodiscard]] static constexpr Instruction astore(std::uint16_t index) noexcept {
    return Instruction(Opcode::AStore, index);
  }

  [[nodiscard]] static std::expected<Instruction, CodeEditError> simple(Opcode opcode);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> immediate(Opcode opcode, std::int32_t value);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> local(Opcode opcode, std::uint16_t index);
  [[nodiscard]] static Instruction increment(std::uint16_t index, std::int16_t amount);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> constant(Opcode opcode, CpIndex index);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> invoke_interface(CpIndex index, std::uint8_t count);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> multi_array(CpIndex index, std::uint8_t dimensions);
  [[nodiscard]] static std::expected<Instruction, CodeEditError> new_array(ArrayType type);
  [[nodiscard]] static std::expected<Branch, CodeEditError> branch(Opcode opcode, LabelId target);

  [[nodiscard]] constexpr Opcode opcode() const {
    return operation;
  }
  [[nodiscard]] constexpr EncodingHint encoding_hint() const {
    return hint;
  }
  [[nodiscard]] std::optional<std::int32_t> immediate_value() const;
  [[nodiscard]] std::optional<std::uint16_t> local_index() const;
  [[nodiscard]] std::optional<Increment> increment_value() const;
  [[nodiscard]] std::optional<CpIndex> constant_index() const;
  [[nodiscard]] std::optional<std::uint8_t> interface_count() const;
  [[nodiscard]] std::optional<std::uint8_t> dimensions() const;
  [[nodiscard]] std::optional<ArrayType> array_type() const;

  friend bool operator==(const Instruction&, const Instruction&) = default;

private:
  friend class MethodCode;
  friend struct detail::BytecodeAccess;

  constexpr Instruction(Opcode operation, std::uint32_t operand_bits, std::uint16_t secondary = 0,
                        EncodingHint hint = EncodingHint::Automatic) :
      operand_bits(operand_bits), secondary(secondary), operation(operation), hint(hint) {
  }

  std::uint32_t operand_bits = 0;
  std::uint16_t secondary = 0;
  Opcode operation = Opcode::Nop;
  EncodingHint hint = EncodingHint::Automatic;
};

static_assert(sizeof(Instruction) == 8);
static_assert(std::is_trivially_copyable_v<Instruction>);

} // namespace cafe
