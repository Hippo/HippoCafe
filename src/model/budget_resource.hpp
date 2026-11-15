#pragma once

#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>

namespace cafe::detail {

class BudgetExceeded : public std::bad_alloc {};

class BudgetResource final : public std::pmr::memory_resource {
public:
  BudgetResource(std::pmr::memory_resource* upstream, std::size_t limit) noexcept
      : upstream(upstream ? upstream : std::pmr::get_default_resource()), limit(limit) {
  }

  void charge(std::size_t size) {
    if (size > limit - used)
      throw BudgetExceeded();
    used += size;
  }

  void disarm() noexcept {
    limit = std::numeric_limits<std::size_t>::max();
  }

private:
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    if (size > limit - used)
      throw BudgetExceeded();
    void* memory = upstream->allocate(size, alignment);
    used += size;
    return memory;
  }

  void do_deallocate(void* memory, std::size_t size, std::size_t alignment) override {
    upstream->deallocate(memory, size, alignment);
    used -= size;
  }

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::pmr::memory_resource* upstream;
  std::size_t limit;
  std::size_t used = 0;
};

} // namespace cafe::detail
