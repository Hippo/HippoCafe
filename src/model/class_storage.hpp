/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup implementation
 */

#pragma once

#include <atomic>
#include <limits>
#include <memory_resource>
#include <utility>
#include <vector>

#include <cafe/class_file.hpp>

#include "budget_resource.hpp"

namespace cafe::detail {

/**
 * @brief Allocate a process-wide declaration identity stamp without wrapping.
 * @return A fresh nonzero stamp, or zero after serial exhaustion.
 * @ingroup implementation
 */
inline std::uint64_t next_declaration_revision() noexcept {
  static std::atomic<std::uint64_t> counter{1};
  auto next = counter.load(std::memory_order_relaxed);
  while (next != std::numeric_limits<std::uint64_t>::max()) {
    if (counter.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
      return next;
  }
  return 0;
}

/**
 * @brief Stable class-owned storage with a read-time budget, metadata arena, immutable original
 * source, constant pool, and ordered declaration tables. Resources precede dependent containers to
 * guarantee destruction order.
 * @ingroup implementation
 */
struct ClassStorage {
  /**
   * @brief Initialize stable class-owned storage with a read-time budget, metadata arena, immutable
   * original source, constant pool, and ordered declaration tables
   * @param source Owned original class-file bytes transferred into class storage.
   * @param upstream Upstream PMR resource that must outlive the owning class; null selects the
   *   default resource.
   * @param limit Maximum accounted byte count for this resource or buffer.
   */
  ClassStorage(std::vector<std::byte> source, std::pmr::memory_resource* upstream,
               std::size_t limit = std::numeric_limits<std::size_t>::max()) : budget(upstream, limit),
                                                                              metadata_arena(&budget),
                                                                              source(std::move(source)),
                                                                              pool(&budget, &metadata_arena),
                                                                              interface_entries(&budget),
                                                                              field_entries(&budget),
                                                                              method_entries(&budget),
                                                                              attribute_entries(&budget) {
    budget.charge(sizeof(ClassStorage));
    budget.charge(this->source.capacity());
  }

  /**
   * @brief Owning forwarding resource enforcing charged allocation bytes.
   *
   * Mutable buffers allocate directly through the budget and release storage
   * as soon as they are destroyed, including after speculative decodes fail.
   */
  BudgetResource budget;
  std::pmr::monotonic_buffer_resource metadata_arena; ///< Monotonic storage for immutable class metadata, backed by the class budget.
  const std::vector<std::byte> source; ///< Owned immutable original class-file input; empty for a programmatic class.

  ClassVersion version; ///< Exact class-file major and minor version.
  std::uint64_t declaration_stamp = next_declaration_revision(); ///< Current class declaration identity stamp.
  std::uint16_t flags = 0; ///< JVM access bit mask.
  CpIndex this_class; ///< Class constant identifying the owner declaration.
  CpIndex super_class; ///< Direct superclass Class constant, or zero where permitted.
  ClassValidity validity = ClassValidity::Programmatic; ///< Parsing provenance and compatible recovery state.
  ConstantPool pool; ///< Owned constant pool backed by this class's allocation resources and source.
  std::pmr::vector<CpIndex> interface_entries; ///< Direct interface Class references in declaration order.
  std::pmr::vector<Field> field_entries; ///< Allocator-owned field declarations in table order.
  std::pmr::vector<Method> method_entries; ///< Allocator-owned method declarations in table order.
  std::pmr::vector<Attribute> attribute_entries; ///< Owned ordered attribute list using the declaration's allocator.
};

} // namespace cafe::detail
