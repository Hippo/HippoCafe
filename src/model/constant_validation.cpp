/**
 * @file
 * @brief Validate instruction operand references and class-level bootstrap rows without mutation.
 * @ingroup implementation
 */

#include "constant_validation.hpp"

#include <limits>
#include <vector>

#include "io/class_format.hpp"

namespace cafe::detail {

OperandValidity ConstantOperandValidator::validate(CpIndex root, const ConstantPool& pool, ClassVersion version,
                                                   std::span<const Attribute> attributes, std::size_t* work) {
  if (constants.contains(root.value))
    return OperandValidity::Valid;
  const auto spend = [work](std::size_t count) {
    if (!work)
      return true;
    if (count > *work)
      return false;
    *work -= count;
    return true;
  };
  std::pmr::vector<CpIndex> pending(constants.get_allocator().resource());
  const auto enqueue = [&](CpIndex index) {
    if (constants.insert(index.value).second)
      pending.push_back(index);
  };
  enqueue(root);
  while (!pending.empty()) {
    if (!spend(1))
      return OperandValidity::WorkLimit;
    const auto index = pending.back();
    pending.pop_back();
    const auto* entry = pool.get(index);
    if (!entry || cp_reference_issue(pool, *entry, version, false))
      return OperandValidity::Invalid;
    const auto format = cp_format(entry->tag);
    if (version.major < format.introduced_major)
      return OperandValidity::UnsupportedVersion;
    if (dynamic_constant(entry->tag)) {
      enqueue(entry->second);
      if (!rows.insert(entry->first.value).second)
        continue;
      if (!table) {
        if (!spend(attributes.size()))
          return OperandValidity::WorkLimit;
        for (const auto& attribute : attributes) {
          if (pool.get_utf8(attribute.name_index()) != "BootstrapMethods")
            continue;
          const auto* candidate = attribute.get<BootstrapMethods>();
          if (table || !candidate)
            return OperandValidity::Invalid;
          table = candidate;
        }
      }
      constexpr auto MAX_COUNT = std::numeric_limits<std::uint16_t>::max();
      if (!table || table->methods.size() > MAX_COUNT || entry->first.value >= table->methods.size())
        return OperandValidity::Invalid;
      const auto& row = table->methods[entry->first.value];
      if (!pool.has(row.method_handle, CpTag::MethodHandle) || row.arguments.size() > MAX_COUNT)
        return OperandValidity::Invalid;
      if (!spend(row.arguments.size()))
        return OperandValidity::WorkLimit;
      enqueue(row.method_handle);
      for (const auto argument : row.arguments) {
        const auto* value = pool.get(argument);
        if (!value || !loadable_constant(value->tag, version))
          return OperandValidity::Invalid;
        enqueue(argument);
      }
      continue;
    }
    switch (format.form) {
      case CpForm::Pair:
        enqueue(entry->second);
        [[fallthrough]];
      case CpForm::Index:
      case CpForm::Handle:
        enqueue(entry->first);
        break;
      case CpForm::Utf8:
        if (!pool.get_utf8(index))
          return OperandValidity::Invalid;
        break;
      case CpForm::Bits32:
      case CpForm::Bits64:
        break;
      default:
        return OperandValidity::Invalid;
    }
  }
  return OperandValidity::Valid;
}

} // namespace cafe::detail
