/**
 * @file
 * @brief Symbolic operands for inspecting and editing class-owned instructions.
 * @ingroup bytecode
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cafe/bytecode/builder.hpp>

namespace cafe {

/**
 * @brief A getstatic, putstatic, getfield, or putfield with a symbolic field reference.
 * @ingroup bytecode
 */
struct FieldAccess {
  Opcode opcode = Opcode::Nop; ///< Field access opcode.
  std::string_view owner; ///< Declaring class or interface in JVM internal spelling.
  std::string_view name; ///< Field name in modified UTF-8.
  std::string_view descriptor; ///< Field descriptor in modified UTF-8.
};

/**
 * @brief An ordinary invocation with a symbolic method reference.
 * @ingroup bytecode
 */
struct MethodInvocation {
  Opcode opcode = Opcode::Nop; ///< invokevirtual, invokespecial, invokestatic, or invokeinterface.
  std::string_view owner; ///< Owner in JVM internal spelling, or an array descriptor for invokevirtual.
  std::string_view name; ///< Method name in modified UTF-8.
  std::string_view descriptor; ///< Method descriptor in modified UTF-8.
  bool owner_is_interface = false; ///< Set for static or special interface calls; invokeinterface implies it.

  /**
   * @brief Compare the exact owner, name and descriptor encoded in the method reference.
   * @param expected_owner JVM internal class name or array descriptor.
   * @param expected_name Target method name.
   * @param expected_descriptor Target method descriptor.
   * @return True when all three strings match.
   */
  [[nodiscard]] constexpr bool matches(std::string_view expected_owner, std::string_view expected_name,
                                       std::string_view expected_descriptor) const noexcept {
    return owner == expected_owner && name == expected_name && descriptor == expected_descriptor;
  }
};

/**
 * @brief JVM method-handle reference kinds, including field access.
 * @ingroup bytecode
 */
enum class MethodHandleKind : std::uint8_t {
  GetField = 1, ///< REF_getField.
  GetStatic = 2, ///< REF_getStatic.
  PutField = 3, ///< REF_putField.
  PutStatic = 4, ///< REF_putStatic.
  InvokeVirtual = 5, ///< REF_invokeVirtual.
  InvokeStatic = 6, ///< REF_invokeStatic.
  InvokeSpecial = 7, ///< REF_invokeSpecial.
  NewInvokeSpecial = 8, ///< REF_newInvokeSpecial, for an instance initializer.
  InvokeInterface = 9, ///< REF_invokeInterface.
};

/**
 * @brief A symbolic JVM method handle for a bootstrap method or argument.
 * @ingroup bytecode
 */
struct MethodHandle {
  MethodHandleKind kind{}; ///< Reference kind selects the target member form.
  std::string_view owner; ///< Owner in JVM internal spelling, or an array descriptor for InvokeVirtual.
  std::string_view name; ///< Member name in modified UTF-8.
  std::string_view descriptor; ///< Member descriptor in modified UTF-8.
  bool owner_is_interface = false; ///< Set for static or special interface methods; InvokeInterface implies it.
};

/**
 * @brief A class literal used as a bootstrap argument.
 * @ingroup bytecode
 */
struct ClassConstant {
  std::string_view name; ///< JVM internal class name or array descriptor in modified UTF-8.
};

/**
 * @brief A method type used as a bootstrap argument.
 * @ingroup bytecode
 */
struct MethodTypeConstant {
  std::string_view descriptor; ///< Method descriptor in modified UTF-8.
};

struct BootstrapArgument;

/**
 * @brief Bootstrap method and its ordered static arguments.
 * @ingroup bytecode
 */
struct Bootstrap {
  MethodHandle handle; ///< Method handle invoked by the JVM during linkage.
  std::span<const BootstrapArgument> arguments = {}; ///< Borrowed static arguments in invocation order.
};

/**
 * @brief A dynamically computed constant used as a load instruction or a bootstrap argument.
 * @ingroup bytecode
 */
struct DynamicConstant {
  std::string_view name; ///< Constant name in modified UTF-8.
  std::string_view descriptor; ///< Field descriptor of the computed value in modified UTF-8.
  Bootstrap bootstrap; ///< Bootstrap method and static arguments.
};

/**
 * @brief One loadable bootstrap argument. Use std::string_view for a Java String in modified UTF-8.
 * @ingroup bytecode
 */
struct BootstrapArgument {
  /// Java int, float, long, double, String, class, method type, method handle, or dynamic constant.
  using Value = std::variant<std::int32_t, float, std::int64_t, double, std::string_view,
                             ClassConstant, MethodTypeConstant, MethodHandle, DynamicConstant>;
  Value value; ///< Argument value.
};

/**
 * @brief An invokedynamic call site with a symbolic bootstrap specifier.
 * @ingroup bytecode
 */
struct DynamicInvocation {
  std::string_view name; ///< Call-site name in modified UTF-8.
  std::string_view descriptor; ///< Call-site method descriptor in modified UTF-8.
  Bootstrap bootstrap; ///< Bootstrap method and static arguments.
};

/**
 * @brief An ldc-family literal to intern in the destination class at edit time.
 * @ingroup bytecode
 */
struct ConstantLoad {
  BootstrapArgument constant; ///< Loadable JVM constant; its category determines ldc or ldc2_w.
};

/**
 * @brief An object or array instruction with a symbolic Class operand.
 * @ingroup bytecode
 */
struct TypeInstruction {
  Opcode opcode; ///< New, ANewArray, CheckCast, InstanceOf, or MultiANewArray.
  std::string_view name; ///< Internal class name or array descriptor in modified UTF-8.
  std::uint8_t dimensions = 0; ///< Nonzero dimension count for MultiANewArray, zero otherwise.
};

/**
 * @brief An instruction accepted by MethodEditor, which resolves symbolic operands in its class.
 *
 * Referenced text, bootstrap arguments, and switch data must remain live through the edit call.
 * The caller supplies the stack operands and the writer can verify the edited method.
 * @ingroup bytecode
 */
using MethodInstruction = std::variant<Instruction, Branch, TableSwitch, LookupSwitch,
                                       FieldAccess, MethodInvocation, DynamicInvocation, DynamicConstant,
                                       ConstantLoad, TypeInstruction, std::unexpected<CodeEditError>>;

/**
 * @brief A class-relative method index and a stable instruction handle within that method.
 * @ingroup bytecode
 */
struct InstructionLocation {
  std::size_t method_index = 0; ///< Current method-table index; erasing methods can shift it.
  InstructionId id; ///< Handle scoped to that method, valid until the instruction is erased.
  friend bool operator==(InstructionLocation, InstructionLocation) = default;
};

/**
 * @brief A symbolic operand obtained by inspecting a field access, call, or dynamic load.
 *
 * Text borrows stable storage from the source class. Bootstrap argument spans borrow this
 * result's storage, which moves with it and survives edits to the source class. Keep both owners
 * alive while using any borrowed operand, and use the destination after moving the result.
 * Parsed bootstrap argument graphs can contain cycles.
 * @ingroup bytecode
 */
class SymbolicInstruction {
public:
  /// Transfer the operand and ownership of any decoded bootstrap arguments.
  SymbolicInstruction(SymbolicInstruction&&) noexcept = default;
  /**
   * @brief Replace this result and invalidate views of its previous bootstrap arguments.
   * @param other Result transferring its operand and arguments.
   * @return This result.
   */
  SymbolicInstruction& operator=(SymbolicInstruction&& other) noexcept = default;
  /// A result owns the bootstrap arrays referenced by its operand.
  SymbolicInstruction(const SymbolicInstruction&) = delete;
  /// A result owns the bootstrap arrays referenced by its operand.
  SymbolicInstruction& operator=(const SymbolicInstruction&) = delete;

  /**
   * @brief Access the FieldAccess, MethodInvocation, DynamicInvocation, or DynamicConstant.
   * @return A borrowed operand that can also be passed to MethodEditor.
   */
  [[nodiscard]] const MethodInstruction& operand() const& noexcept {
    return value;
  }
  /// Access requires a live result to retain the borrowed bootstrap argument arrays.
  const MethodInstruction& operand() const&& = delete;

private:
  friend class ClassFile;

  /**
   * @brief Retain an inspected operand and any arrays backing its bootstrap views.
   * @param value Decoded symbolic instruction.
   * @param arguments Argument arrays for the reachable bootstrap rows.
   */
  explicit SymbolicInstruction(MethodInstruction value,
                               std::vector<std::vector<BootstrapArgument>> arguments = {}) noexcept
      : value(value), arguments(std::move(arguments)) {
  }

  MethodInstruction value; ///< The root symbolic operand.
  std::vector<std::vector<BootstrapArgument>> arguments; ///< One stable buffer per reachable bootstrap row.
};

} // namespace cafe
