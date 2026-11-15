#pragma once

#include <expected>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <cafe/analysis/analysis_error.hpp>
#include <cafe/analysis/frame.hpp>
#include <cafe/bytecode/instruction.hpp>
#include <cafe/class_file.hpp>

namespace cafe::classpath {
class ClassHierarchy;
}

namespace cafe::analysis {

// One simulator per method. A null hierarchy selects the hierarchy-independent
// stack analysis. The owner and scratch resource must outlive the simulator.
// Initialize the entry frame before preparing and applying instructions.
class InstructionEffects {
public:
  InstructionEffects(const ClassFile& owner, const Method& method, const classpath::ClassHierarchy* hierarchy,
                     std::pmr::memory_resource* scratch);

  [[nodiscard]] std::expected<void, AnalysisError> initialize(Frame& frame, std::uint16_t max_locals);
  // Validate static operands in code order, including those in unreachable
  // blocks, and cache descriptor-dependent effects before the worklist runs.
  [[nodiscard]] std::expected<void, AnalysisError> prepare(InstructionId at, Instruction instruction,
                                                           std::uint16_t max_locals);
  // Mutates the normal successor state; a successful constructor call returns
  // the pre-instruction receiver identity for the exceptional transfer.
  [[nodiscard]] std::expected<std::optional<VerificationType>, AnalysisError>
  apply(Frame& frame, InstructionId at, Instruction instruction);

private:
  struct InvocationShape {
    explicit InvocationShape(std::pmr::memory_resource* scratch) : arguments(scratch) {
    }

    std::pmr::vector<VerificationType> arguments;
    VerificationType result;
    unsigned parameter_slots = 0;
  };

  struct ConstantEffect {
    VerificationType value;
    std::string_view member_class;
    std::string_view member_name;
    std::string_view member_descriptor;
    std::span<const VerificationType> arguments;
    // Exact invokeinterface count or maximum multianewarray dimensions.
    unsigned auxiliary_limit = 0;
    bool constructor = false;
    bool declared_here = false;
    std::optional<bool> protected_receiver{};
  };

  using Status = std::expected<void, AnalysisError>;
  using ValueResult = std::expected<VerificationType, AnalysisError>;

  [[nodiscard]] std::expected<ConstantEffect, AnalysisError> read_constant(Instruction instruction);
  [[nodiscard]] Status execute(Frame& frame, InstructionId at, Instruction instruction,
                               std::optional<VerificationType>& initialized);
  [[nodiscard]] Status execute_constant(Frame& frame, InstructionId at, Instruction instruction,
                                        ConstantEffect& effect, std::optional<VerificationType>& initialized);
  [[nodiscard]] Status execute_field(Frame& frame, Opcode opcode, ConstantEffect& effect);
  [[nodiscard]] Status execute_invoke(Frame& frame, Opcode opcode, ConstantEffect& effect,
                                      std::optional<VerificationType>& initialized);
  [[nodiscard]] Status initialize_receiver(Frame& frame, ConstantEffect& effect,
                                           std::optional<VerificationType>& initialized);
  [[nodiscard]] std::expected<bool, AnalysisError> requires_protected_receiver(const ConstantEffect& effect, bool method);
  [[nodiscard]] Status check_protected_receiver(VerificationType receiver, ConstantEffect& effect, bool method);
  [[nodiscard]] Status validate_special_owner(CpTag tag, std::string_view target);
  [[nodiscard]] Status execute_array_access(Frame& frame, Opcode opcode);
  [[nodiscard]] Status execute_return(Frame& frame, Opcode opcode);
  [[nodiscard]] Status match(VerificationType value, VerificationType expected,
                             bool allow_uninitialized = false);
  [[nodiscard]] ValueResult pop_matching(Frame& frame, VerificationType expected, bool allow_uninitialized = false);
  [[nodiscard]] Status transition(Frame& frame, std::initializer_list<VerificationType> popped,
                                  std::optional<VerificationType> pushed = std::nullopt);
  [[nodiscard]] std::string_view array_of(std::string_view component);

  const ClassFile& owner;
  const Method& method;
  const classpath::ClassHierarchy* hierarchy;
  std::pmr::memory_resource* scratch;
  std::string_view self;
  std::string_view superclass;
  VerificationType return_type;
  std::pmr::unordered_set<std::pmr::string> array_names;
  std::pmr::unordered_map<std::uint16_t, InvocationShape> method_descriptors;
  std::pmr::unordered_map<std::uint32_t, ConstantEffect> constants;
  std::optional<std::pmr::vector<std::shared_ptr<const classpath::ClassInfo>>> superclass_chain;
};

} // namespace cafe::analysis
