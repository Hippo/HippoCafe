/**
 * @file
 * @brief Owning, class-independent construction of a sequence of method instructions.
 * @ingroup bytecode
 */
#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include <cafe/bytecode/insn.hpp>

namespace cafe {
namespace detail {
struct BuilderAccess;
}

/**
 * @brief A boundary in a pending CodeBuilder sequence. A default label is invalid.
 * @ingroup bytecode
 */
class BuilderLabel {
public:
  /// Create an invalid label.
  BuilderLabel() noexcept = default;

private:
  friend class CodeBuilder;
  /// @param owner Builder storage that issued the label.
  /// @param ordinal Index of the binding in the builder.
  BuilderLabel(const std::shared_ptr<const void>& owner, std::size_t ordinal) noexcept : owner(owner), ordinal(ordinal) {
  }
  std::weak_ptr<const void> owner; ///< Identity of the sequence that created the label.
  std::size_t ordinal{}; ///< Index into that sequence's label bindings.
};

/**
 * @brief A sparse-switch case targeting another boundary in the same builder.
 * @ingroup bytecode
 */
struct BuilderLookupPair {
  std::int32_t key; ///< Signed JVM switch key.
  BuilderLabel target; ///< Builder-local case destination.
};

/**
 * @brief An owning sequence that can be constructed and reused independently of a class.
 *
 * Mnemonics use the same operands as cafe::insn. Emission copies text, switch data, and bootstrap
 * arguments immediately. A method editor resolves constants and branch targets when applying the
 * sequence. Builder-local labels survive moves. Method-scoped LabelId and raw pool indices remain
 * specific to their original owners. The editor reports the first construction error.
 * @ingroup bytecode
 */
class CodeBuilder {
public:
  /// Start an empty, independent sequence.
  CodeBuilder() = default;
  /// Transfer the sequence and its label identity. The source becomes empty.
  CodeBuilder(CodeBuilder&&) noexcept = default;
  /// Replace the sequence with another. Labels of the replaced sequence become invalid.
  /// @return This builder after taking ownership.
  CodeBuilder& operator=(CodeBuilder&&) noexcept = default;
  /// A builder has one mutable owner.
  CodeBuilder(const CodeBuilder&) = delete;
  /// Assignment transfers builder ownership only through a move.
  CodeBuilder& operator=(const CodeBuilder&) = delete;

  /// Create a new unbound label in this builder.
  /// @return A label scoped to this builder.
  [[nodiscard]] BuilderLabel label();
  /// Bind a builder-local label to the next instruction, or to the successor of the applied sequence.
  /// @param target Label allocated by this builder.
  /// @return This builder for chaining.
  CodeBuilder& bind(const BuilderLabel& target);
  /// Append one instruction, retaining all borrowed input data.
  /// @param instruction Raw or symbolic instruction to emit.
  /// @return This builder for chaining.
  CodeBuilder& emit(const MethodInstruction& instruction);

#define HIPPOCAFE_INSN_SIMPLE(name, operation)                                           \
  /** @brief Append the JVM operation instruction. @return This builder for chaining. */ \
  CodeBuilder& name();
#define HIPPOCAFE_INSN_LOCAL(name, operation)                                                                              \
  /** @brief Append the JVM operation instruction. @param index Local variable slot. @return This builder for chaining. */ \
  CodeBuilder& name(std::integral auto index) {                                                                            \
    return emit(insn::name(index));                                                                                        \
  }
#define HIPPOCAFE_INSN_BRANCH(name, operation)                                                                                        \
  /** @brief Append the JVM operation instruction. @param target Executable branch destination. @return This builder for chaining. */ \
  CodeBuilder& name(LabelId target);                                                                                                  \
  /** @brief Append the JVM operation instruction. @param target Executable branch destination. @return This builder for chaining. */ \
  CodeBuilder& name(const BuilderLabel& target);
#define HIPPOCAFE_INSN_FIELD(name, operation)                                                                                                                                                    \
  /** @brief Append the JVM operation instruction. @param owner Internal JVM owner name. @param member Field name. @param descriptor JVM field descriptor. @return This builder for chaining. */ \
  CodeBuilder& name(std::string_view owner, std::string_view member, std::string_view descriptor);
#define HIPPOCAFE_INSN_TYPE(name, operation)                                                                                                            \
  /** @brief Append the JVM operation instruction. @param class_name JVM internal class name or array descriptor. @return This builder for chaining. */ \
  CodeBuilder& name(std::string_view class_name);
#include <cafe/bytecode/detail/insn_families.inc>
  HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_EXPAND_MNEMONICS
#undef HIPPOCAFE_INSN_SIMPLE
#undef HIPPOCAFE_INSN_LOCAL
#undef HIPPOCAFE_INSN_BRANCH
#undef HIPPOCAFE_INSN_FIELD
#undef HIPPOCAFE_INSN_TYPE

  /// Push an int encoded as a signed byte.
  /// @param value JVM value to load.
  /// @return This builder for chaining.
  CodeBuilder& bipush(std::integral auto value) {
    return emit(insn::bipush(value));
  }
  /// Push an int encoded as a signed short.
  /// @param value JVM value to load.
  /// @return This builder for chaining.
  CodeBuilder& sipush(std::integral auto value) {
    return emit(insn::sipush(value));
  }
  /// Increment a local int by a signed amount.
  /// @param index Local variable slot.
  /// @param amount Signed increment.
  /// @return This builder for chaining.
  CodeBuilder& iinc(std::integral auto index, std::integral auto amount) {
    return emit(insn::iinc(index, amount));
  }
  /// Load a symbolic JVM literal. Text and bootstrap arguments are copied now.
  /// @param value JVM value to load.
  /// @return This builder for chaining.
  template<typename Literal>
    requires requires(Literal&& value) { insn::ldc(std::forward<Literal>(value)); }
  CodeBuilder& ldc(Literal&& value) {
    return emit(insn::ldc(std::forward<Literal>(value)));
  }

  /// Emit an ordinary virtual method invocation.
  /// @param owner Internal JVM owner name.
  /// @param name JVM method name.
  /// @param descriptor JVM method descriptor.
  /// @return This builder for chaining.
  CodeBuilder& invokevirtual(std::string_view owner, std::string_view name, std::string_view descriptor);
  /// Emit a static class or interface method invocation.
  /// @param owner Internal JVM owner name.
  /// @param name JVM method name.
  /// @param descriptor JVM method descriptor.
  /// @param owner_is_interface Whether the owner names an interface.
  /// @return This builder for chaining.
  CodeBuilder& invokestatic(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface = false);
  /// Emit a constructor, superclass method, or special interface method invocation.
  /// @param owner Internal JVM owner name.
  /// @param name JVM method name.
  /// @param descriptor JVM method descriptor.
  /// @param owner_is_interface Whether the owner names an interface.
  /// @return This builder for chaining.
  CodeBuilder& invokespecial(std::string_view owner, std::string_view name, std::string_view descriptor, bool owner_is_interface = false);
  /// Emit an interface invocation.
  /// @param owner Internal JVM owner name.
  /// @param name JVM method name.
  /// @param descriptor JVM method descriptor.
  /// @return This builder for chaining.
  CodeBuilder& invokeinterface(std::string_view owner, std::string_view name, std::string_view descriptor);
  /// Emit a dynamic invocation, retaining its bootstrap arguments.
  /// @param name JVM method name.
  /// @param descriptor JVM method descriptor.
  /// @param bootstrap Bootstrap handle and static arguments.
  /// @return This builder for chaining.
  CodeBuilder& invokedynamic(std::string_view name, std::string_view descriptor, Bootstrap bootstrap);
  /// Create a primitive array.
  /// @param type Primitive array element kind.
  /// @return This builder for chaining.
  CodeBuilder& newarray(ArrayType type);
  /// Allocate one or more dimensions of an array.
  /// @param descriptor JVM array descriptor.
  /// @param dimensions Number of array dimensions to allocate.
  /// @return This builder for chaining.
  CodeBuilder& multianewarray(std::string_view descriptor, std::integral auto dimensions) {
    return emit(insn::multianewarray(descriptor, dimensions));
  }
  /// Emit a dense switch to method-scoped labels.
  /// @param low First consecutive signed switch key.
  /// @param default_target Destination for keys outside the supplied cases.
  /// @param targets Destinations in consecutive key order.
  /// @return This builder for chaining.
  CodeBuilder& tableswitch(std::integral auto low, LabelId default_target, std::span<const LabelId> targets) {
    return emit(insn::tableswitch(low, default_target, targets));
  }
  /// Emit a dense switch that can be replayed in other methods.
  /// @param low First consecutive signed switch key.
  /// @param default_target Builder-local default destination.
  /// @param targets Builder-local destinations in consecutive key order.
  /// @return This builder for chaining.
  CodeBuilder& tableswitch(std::integral auto low, const BuilderLabel& default_target, std::span<const BuilderLabel> targets) {
    if (!std::in_range<std::int32_t>(+low))
      return emit(std::unexpected(CodeEditError::InvalidOperand));
    return local_table(static_cast<std::int32_t>(low), default_target, targets);
  }
  /// Emit a sparse switch to method-scoped labels.
  /// @param default_target Destination for keys outside the supplied cases.
  /// @param pairs Sorted signed keys and their destinations.
  /// @return This builder for chaining.
  CodeBuilder& lookupswitch(LabelId default_target, std::span<const LookupPair> pairs);
  /// Emit a sparse switch that can be replayed in other methods.
  /// @param default_target Builder-local default destination.
  /// @param pairs Sorted signed keys and their builder-local destinations.
  /// @return This builder for chaining.
  CodeBuilder& lookupswitch(const BuilderLabel& default_target, std::span<const BuilderLookupPair> pairs);

private:
  friend struct detail::BuilderAccess;
  /// Stable storage for instruction payloads and pending label fixups.
  struct Storage;
  /// @return Pending sequence storage, allocated on first use.
  Storage& state();
  /// @param opcode Logical single-target branch opcode.
  /// @param target Label allocated by this builder.
  /// @return This builder after recording the unresolved branch.
  CodeBuilder& local_branch(Opcode opcode, const BuilderLabel& target);
  /// Append a dense switch after checking the JVM key range.
  /// @param low First consecutive signed switch key.
  /// @param default_target Builder-local default destination.
  /// @param targets Builder-local case destinations.
  /// @return This builder for chaining.
  CodeBuilder& local_table(std::int32_t low, const BuilderLabel& default_target, std::span<const BuilderLabel> targets);
  /// Stage builder-local target fixups and publish one pending branch or switch.
  /// @param first Ordinary branch target or default switch target.
  /// @param cases Additional switch targets in operand order.
  /// @param publish Appends the instruction and any out-of-line switch payload.
  /// @return This builder for chaining.
  template<typename Targets, typename Publish>
  CodeBuilder& with_local_targets(const BuilderLabel& first, const Targets& cases, Publish publish);
  std::shared_ptr<Storage> storage; ///< Lazily allocated, identified by builder-local labels.
};

} // namespace cafe
