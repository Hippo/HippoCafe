/**
 * @file
 * @brief Read-only validation of reachable instruction operand constants.
 * @ingroup implementation
 */

#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>
#include <unordered_set>

#include <cafe/attribute.hpp>
#include <cafe/class_version.hpp>
#include <cafe/constant_pool.hpp>

namespace cafe::detail {

/// Outcome of walking the constant and bootstrap dependencies of one operand.
enum class OperandValidity {
  Valid, ///< Every reachable reference has a compatible structure.
  Invalid, ///< A constant, row, or reference is malformed.
  UnsupportedVersion, ///< A constant tag is unavailable in the declared class version.
  WorkLimit, ///< The caller's search work allowance was exhausted.
};

/// Validates a class's reachable constant graph incrementally during one search or edit.
/// Existing entries must be immutable throughout the operation. Discard the validator on failure.
class ConstantOperandValidator {
public:
  /// @param resource Temporary allocator for the visited sets and graph traversal.
  explicit ConstantOperandValidator(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) :
      constants(resource), rows(resource) {
  }

  /// @param root Constant-pool index of an instruction operand.
  /// @param pool Class owning the entries.
  /// @param version Declared class-file version.
  /// @param attributes Current class attributes, including BootstrapMethods when required.
  /// @param work Remaining search work, or null for an unrestricted editing operation.
  /// @return Validation outcome, with cycles visited once and shared references reused.
  OperandValidity validate(CpIndex root, const ConstantPool& pool, ClassVersion version,
                           std::span<const Attribute> attributes, std::size_t* work = nullptr);

private:
  std::pmr::unordered_set<std::uint16_t> constants; ///< Pool indices already visited.
  std::pmr::unordered_set<std::uint16_t> rows; ///< Visited bootstrap row ordinals.
  const BootstrapMethods* table = nullptr; ///< The unique typed bootstrap table, when needed.
};

} // namespace cafe::detail
