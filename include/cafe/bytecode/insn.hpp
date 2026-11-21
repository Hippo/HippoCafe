/**
 * @file
 * @brief JVM mnemonic factories for instructions accepted by MethodEditor and CodeBuilder.
 * @ingroup bytecode
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <cafe/bytecode/method_instruction.hpp>

/**
 * @brief Construct method instructions without a class or a constant pool.
 *
 * These values borrow supplied text, switch arrays, and bootstrap arguments until submitted.
 * A CodeBuilder owns a copy upon emission. An out-of-range numeric operand produces a pending
 * CodeEditError which the editor reports with its position in a submitted sequence.
 * @ingroup bytecode
 */
namespace cafe::insn {
namespace detail {

/// Convert the raw factory result without throwing on invalid user input.
/// @param result Instruction construction result.
/// @return The instruction or its deferred validation error.
inline MethodInstruction from_instruction(std::expected<Instruction, CodeEditError> result) noexcept {
  if (!result)
    return std::unexpected(result.error());
  return *result;
}

} // namespace detail

// These families differ only by the fixed opcode. The same mnemonic table supplies the
// corresponding chainable CodeBuilder overloads.
#define HIPPOCAFE_INSN_SIMPLE(name, operation)                                                      \
  /** @brief Construct the JVM operation instruction. @return The pending operation instruction. */ \
  [[nodiscard]] inline MethodInstruction name() noexcept {                                          \
    return detail::from_instruction(Instruction::simple(Opcode::operation));                        \
  }
#define HIPPOCAFE_INSN_LOCAL(name, operation)                                                                                         \
  /** @brief Construct the JVM operation instruction. @param index Local variable slot. @return The pending operation instruction. */ \
  [[nodiscard]] inline MethodInstruction name(std::integral auto index) noexcept {                                                    \
    if (!std::in_range<std::uint16_t>(+index))                                                                                        \
      return std::unexpected(CodeEditError::InvalidOperand);                                                                          \
    return detail::from_instruction(Instruction::local(Opcode::operation, static_cast<std::uint16_t>(index)));                        \
  }
#define HIPPOCAFE_INSN_BRANCH(name, operation)                                                                                                   \
  /** @brief Construct the JVM operation instruction. @param target Executable branch destination. @return The pending operation instruction. */ \
  [[nodiscard]] inline MethodInstruction name(LabelId target) noexcept {                                                                         \
    return Branch{Opcode::operation, target};                                                                                                    \
  }
#define HIPPOCAFE_INSN_FIELD(name, operation)                                                                                                                                                               \
  /** @brief Construct the JVM operation instruction. @param owner Internal JVM owner name. @param member Field name. @param descriptor JVM field descriptor. @return The pending operation instruction. */ \
  [[nodiscard]] inline MethodInstruction name(std::string_view owner, std::string_view member, std::string_view descriptor) noexcept {                                                                      \
    return FieldAccess{Opcode::operation, owner, member, descriptor};                                                                                                                                       \
  }
#define HIPPOCAFE_INSN_TYPE(name, operation)                                                                                                                       \
  /** @brief Construct the JVM operation instruction. @param class_name JVM internal class name or array descriptor. @return The pending operation instruction. */ \
  [[nodiscard]] inline MethodInstruction name(std::string_view class_name) noexcept {                                                                              \
    return TypeInstruction{Opcode::operation, class_name};                                                                                                         \
  }
#include <cafe/bytecode/detail/insn_families.inc>
HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_INSN_TYPE
#undef HIPPOCAFE_INSN_FIELD
#undef HIPPOCAFE_INSN_BRANCH
#undef HIPPOCAFE_INSN_LOCAL
#undef HIPPOCAFE_INSN_SIMPLE

/// Push a Java int encoded as a signed byte.
/// @param value JVM value to load.
/// @return A byte push, or InvalidOperand when the value exceeds the signed byte range.
[[nodiscard]] inline MethodInstruction bipush(std::integral auto value) noexcept {
  if (!std::in_range<std::int8_t>(+value))
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction::bipush(static_cast<std::int8_t>(value));
}

/// Push a Java int encoded as a signed short.
/// @param value JVM value to load.
/// @return A short push, or InvalidOperand when the value exceeds the signed short range.
[[nodiscard]] inline MethodInstruction sipush(std::integral auto value) noexcept {
  if (!std::in_range<std::int16_t>(+value))
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction::sipush(static_cast<std::int16_t>(value));
}

/// Increment a local int by a signed amount; the writer selects the operand width.
/// @param index Local variable slot.
/// @param amount Signed increment.
/// @return An increment instruction, or InvalidOperand when an operand exceeds its wire range.
[[nodiscard]] inline MethodInstruction iinc(std::integral auto index, std::integral auto amount) noexcept {
  if (!std::in_range<std::uint16_t>(+index) || !std::in_range<std::int16_t>(+amount))
    return std::unexpected(CodeEditError::InvalidOperand);
  return Instruction::increment(static_cast<std::uint16_t>(index), static_cast<std::int16_t>(amount));
}

/// Load a JVM literal with the same payload types used for bootstrap arguments.
/// @param constant Symbolic literal to materialize.
/// @return A deferred literal load.
[[nodiscard]] inline MethodInstruction ldc(BootstrapArgument constant) noexcept {
  if (const auto* dynamic = std::get_if<DynamicConstant>(&constant.value))
    return *dynamic;
  return ConstantLoad{constant};
}

/// Use an int constant for integer types up to 32 bits and a long for wider integer types.
/// @param value JVM value to load.
/// @return An integer or long literal load, or InvalidOperand for an unrepresentable value.
template<std::integral Integer>
[[nodiscard]] MethodInstruction ldc(Integer value) noexcept {
  using JavaInteger = std::conditional_t<(sizeof(Integer) <= sizeof(std::int32_t)), std::int32_t, std::int64_t>;
  if (!std::in_range<JavaInteger>(+value))
    return std::unexpected(CodeEditError::InvalidOperand);
  return ldc(BootstrapArgument{static_cast<JavaInteger>(value)});
}

/// Load a float, double, class literal, method type, method handle, or dynamically computed constant.
/// @param value JVM value to load.
/// @return A deferred literal load.
template<typename T>
  requires(std::same_as<T, float> || std::same_as<T, double> || std::same_as<T, ClassConstant> ||
           std::same_as<T, MethodTypeConstant> || std::same_as<T, MethodHandle> || std::same_as<T, DynamicConstant>)
[[nodiscard]] MethodInstruction ldc(T value) noexcept {
  return ldc(BootstrapArgument{value});
}

/// Load an interned Java String from modified UTF-8 text.
/// @param value Java string contents in modified UTF-8.
/// @return A string literal load that borrows the text.
[[nodiscard]] inline MethodInstruction ldc(std::string_view value) noexcept {
  return ldc(BootstrapArgument{value});
}

/// Invoke an ordinary virtual method.
/// @param owner Internal JVM owner name.
/// @param name JVM method name.
/// @param descriptor JVM method descriptor.
/// @return A virtual invocation with borrowed symbolic operands.
[[nodiscard]] inline MethodInstruction invokevirtual(std::string_view owner, std::string_view name, std::string_view descriptor) noexcept {
  return MethodInvocation{Opcode::InvokeVirtual, owner, name, descriptor};
}

/// Invoke a static method on a class or, from class version 52, an interface.
/// @param owner Internal JVM owner name.
/// @param name JVM method name.
/// @param descriptor JVM method descriptor.
/// @param owner_is_interface Whether the owner names an interface.
/// @return A static invocation with borrowed symbolic operands.
[[nodiscard]] inline MethodInstruction invokestatic(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface = false) noexcept {
  return MethodInvocation{Opcode::InvokeStatic, owner, name, descriptor, owner_is_interface};
}

/// Invoke a constructor, superclass method, or special interface method.
/// @param owner Internal JVM owner name.
/// @param name JVM method name.
/// @param descriptor JVM method descriptor.
/// @param owner_is_interface Whether the owner names an interface.
/// @return A special invocation with borrowed symbolic operands.
[[nodiscard]] inline MethodInstruction invokespecial(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface = false) noexcept {
  return MethodInvocation{Opcode::InvokeSpecial, owner, name, descriptor, owner_is_interface};
}

/// Invoke an interface method. The editor derives the argument-slot count.
/// @param owner Internal JVM owner name.
/// @param name JVM method name.
/// @param descriptor JVM method descriptor.
/// @return An interface invocation with borrowed symbolic operands.
[[nodiscard]] inline MethodInstruction invokeinterface(std::string_view owner, std::string_view name, std::string_view descriptor) noexcept {
  return MethodInvocation{Opcode::InvokeInterface, owner, name, descriptor, true};
}

/// Invoke a dynamic call site using its symbolic bootstrap specification.
/// @param name JVM method name.
/// @param descriptor JVM method descriptor.
/// @param bootstrap Bootstrap handle and static arguments.
/// @return A dynamic invocation retaining the supplied bootstrap view.
[[nodiscard]] inline MethodInstruction invokedynamic(std::string_view name, std::string_view descriptor, Bootstrap bootstrap) noexcept {
  return DynamicInvocation{name, descriptor, bootstrap};
}

/// Create an array with primitive elements.
/// @param type Primitive array element kind.
/// @return A primitive array allocation instruction or InvalidOperand.
[[nodiscard]] inline MethodInstruction newarray(ArrayType type) noexcept {
  return detail::from_instruction(Instruction::new_array(type));
}

/// Allocate dimensions of the supplied JVM array descriptor.
/// @param descriptor JVM array descriptor.
/// @param dimensions Number of array dimensions to allocate.
/// @return A symbolic array allocation, or InvalidOperand for an unencodable dimension count.
[[nodiscard]] inline MethodInstruction multianewarray(std::string_view descriptor, std::integral auto dimensions) noexcept {
  if (!std::in_range<std::uint8_t>(+dimensions) || dimensions == 0)
    return std::unexpected(CodeEditError::InvalidOperand);
  return TypeInstruction{Opcode::MultiANewArray, descriptor, static_cast<std::uint8_t>(dimensions)};
}

/// Dispatch to consecutive switch targets beginning at a signed int key.
/// @param low First consecutive signed switch key.
/// @param default_target Destination for keys outside the supplied cases.
/// @param targets Destinations in consecutive key order.
/// @return A dense switch with borrowed targets, or InvalidOperand for an unencodable key.
[[nodiscard]] inline MethodInstruction tableswitch(std::integral auto low, LabelId default_target, std::span<const LabelId> targets) noexcept {
  if (!std::in_range<std::int32_t>(+low))
    return std::unexpected(CodeEditError::InvalidOperand);
  return TableSwitch{static_cast<std::int32_t>(low), default_target, targets};
}

/// Dispatch to sorted signed switch keys and their method-scoped targets.
/// @param default_target Destination for keys outside the supplied cases.
/// @param pairs Sorted signed keys and their destinations.
/// @return A sparse switch with borrowed key/target pairs.
[[nodiscard]] inline MethodInstruction lookupswitch(LabelId default_target, std::span<const LookupPair> pairs) noexcept {
  return LookupSwitch{default_target, pairs};
}

} // namespace cafe::insn
