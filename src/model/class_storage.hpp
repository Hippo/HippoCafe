#pragma once

#include <atomic>
#include <limits>
#include <memory_resource>
#include <utility>
#include <vector>

#include <cafe/class_file.hpp>

#include "budget_resource.hpp"

namespace cafe::detail {

inline std::uint64_t next_declaration_revision() noexcept {
  static std::atomic<std::uint64_t> counter{1};
  auto next = counter.load(std::memory_order_relaxed);
  while (next != std::numeric_limits<std::uint64_t>::max()) {
    if (counter.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
      return next;
  }
  return 0;
}

struct ClassStorage {
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

  // Mutable buffers allocate directly through the budget and release storage
  // as soon as they are destroyed, including after speculative decodes fail.
  BudgetResource budget;
  std::pmr::monotonic_buffer_resource metadata_arena;
  const std::vector<std::byte> source;

  ClassVersion version;
  std::uint64_t declaration_stamp = next_declaration_revision();
  std::uint16_t flags = 0;
  CpIndex this_class;
  CpIndex super_class;
  ClassValidity validity = ClassValidity::Programmatic;
  ConstantPool pool;
  std::pmr::vector<CpIndex> interface_entries;
  std::pmr::vector<Field> field_entries;
  std::pmr::vector<Method> method_entries;
  std::pmr::vector<Attribute> attribute_entries;
};

} // namespace cafe::detail
