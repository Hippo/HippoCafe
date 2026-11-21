/**
 * @file
 * @brief Own deferred symbolic operands and their reachable bootstrap arguments.
 * @ingroup implementation
 */
#include "symbol_storage.hpp"

#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include "budget_resource.hpp"

namespace cafe::detail {
namespace {

/// Deep-copy even cyclic bootstrap arguments without recursing through caller-provided graphs.
class SymbolCopier {
public:
  /**
   * @brief Initialize a graph-copy worklist.
   * @param backing Stable destination storage.
   * @param max_work Maximum reachable bootstrap argument count.
   * @param cancellation Optional cooperative stop token.
   */
  SymbolCopier(SymbolStorage& backing, std::size_t max_work, std::stop_token cancellation) :
      backing(backing), max_work(max_work), cancellation(std::move(cancellation)),
      memo(backing.names.get_allocator().resource()), pending(backing.names.get_allocator().resource()) {
  }

  /**
   * @brief Clone a symbolic instruction and all reachable bootstrap arrays.
   * @param result Instruction whose symbolic views will refer to owned storage.
   * @return False if cancellation was requested while copying the graph.
   */
  bool own(MethodInstruction& result) {
    if (const auto* f = std::get_if<FieldAccess>(&result)) {
      result = FieldAccess{f->opcode, name(f->owner), name(f->name), name(f->descriptor)};
    } else if (const auto* m = std::get_if<MethodInvocation>(&result)) {
      result = MethodInvocation{m->opcode, name(m->owner), name(m->name), name(m->descriptor), m->owner_is_interface};
    } else if (const auto* d = std::get_if<DynamicInvocation>(&result)) {
      result = DynamicInvocation{name(d->name), name(d->descriptor), bootstrap(d->bootstrap)};
    } else if (const auto* d = std::get_if<DynamicConstant>(&result)) {
      result = dynamic(*d);
    } else if (const auto* constant = std::get_if<ConstantLoad>(&result)) {
      result = ConstantLoad{copy_argument(constant->constant)};
    } else if (const auto* type = std::get_if<TypeInstruction>(&result)) {
      result = TypeInstruction{type->opcode, name(type->name), type->dimensions};
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      if (cancellation.stop_requested())
        return false;
      const auto [src, dst] = pending[i];
      for (std::size_t j = 0; j < src.size(); ++j)
        (*dst)[j] = copy_argument(src[j]);
    }
    return true;
  }

private:
  /// Copy a bootstrap argument, enqueueing any nested argument arrays.
  /// @param arg Argument from the source graph.
  /// @return Argument whose reachable payload will reside in destination storage.
  BootstrapArgument copy_argument(const BootstrapArgument& arg) {
    return BootstrapArgument{std::visit([&](const auto& value) -> BootstrapArgument::Value {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, std::string_view>)
        return name(value);
      else if constexpr (std::is_same_v<T, MethodTypeConstant>)
        return MethodTypeConstant{name(value.descriptor)};
      else if constexpr (std::is_same_v<T, ClassConstant>)
        return ClassConstant{name(value.name)};
      else if constexpr (std::is_same_v<T, MethodHandle>)
        return handle(value);
      else if constexpr (std::is_same_v<T, DynamicConstant>)
        return dynamic(value);
      else
        return value;
    },
                                        arg.value)};
  }

  /**
   * @brief Retain a possibly short string in stable node storage.
   * @param text Caller-owned bytes.
   * @return Stable graph-owned view.
   */
  std::string_view name(std::string_view text) {
    if (text.empty())
      return {};
    return backing.names.emplace_back(text);
  }
  /**
   * @brief Retain a method-handle constant.
   * @param h Source handle.
   * @return Handle whose text belongs to the destination.
   */
  MethodHandle handle(MethodHandle h) {
    return {h.kind, name(h.owner), name(h.name), name(h.descriptor), h.owner_is_interface};
  }
  /**
   * @brief Retain a dynamically computed constant.
   * @param d Source constant.
   * @return Owned symbolic view.
   */
  DynamicConstant dynamic(const DynamicConstant& d) {
    return {name(d.name), name(d.descriptor), bootstrap(d.bootstrap)};
  }
  /**
   * @brief Reserve a stable destination array before enqueueing its arguments.
   * @param b Source bootstrap handle and arguments.
   * @return Bootstrap backed by graph-owned memory.
   */
  Bootstrap bootstrap(const Bootstrap& b) {
    Bootstrap result{handle(b.handle)};
    if (b.arguments.empty())
      return result;
    const auto [begin, end] = memo.equal_range(b.arguments.data());
    for (auto it = begin; it != end; ++it) {
      if (it->second->size() == b.arguments.size()) {
        result.arguments = *it->second;
        return result;
      }
    }
    if (b.arguments.size() > max_work - work)
      throw BudgetExceeded();
    work += b.arguments.size();
    auto& args = backing.arguments.emplace_back();
    args.resize(b.arguments.size());
    memo.emplace(b.arguments.data(), &args);
    pending.emplace_back(b.arguments, &args);
    result.arguments = args;
    return result;
  }
  SymbolStorage& backing; ///< Destination operand backing.
  std::size_t max_work; ///< Maximum total reachable arguments.
  std::stop_token cancellation; ///< Optional cooperative stop token.
  using Arguments = std::pmr::vector<BootstrapArgument>; ///< Allocator-owned copied argument array.
  std::pmr::unordered_multimap<const BootstrapArgument*, Arguments*> memo; ///< Materialized ranges indexed by source array address.
  std::pmr::vector<std::pair<std::span<const BootstrapArgument>, Arguments*>> pending; ///< Source arrays awaiting element copying.
  std::size_t work = 0; ///< Total reachable arguments charged to the copy.
};

} // namespace

MethodInstruction SymbolStorage::copy(const MethodInstruction& value) {
  MethodInstruction result = value;
  SymbolCopier(*this, std::numeric_limits<std::size_t>::max(), {}).own(result);
  return result;
}

std::optional<MethodInstruction> SymbolStorage::copy(const MethodInstruction& value, std::size_t max_work, std::stop_token cancellation) {
  MethodInstruction result = value;
  if (!SymbolCopier(*this, max_work, std::move(cancellation)).own(result))
    return std::nullopt;
  return result;
}

} // namespace cafe::detail
