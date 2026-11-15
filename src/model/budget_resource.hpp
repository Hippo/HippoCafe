/**
 * @file
 * @brief Owned class models, constant-pool storage, and attribute lifetimes.
 * @ingroup implementation
 */

#pragma once

#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>

namespace cafe::detail {

/**
 * @brief Distinguishable allocation-budget exhaustion translated at parser/analysis operation
 * boundaries.
 * @ingroup implementation
 */
class BudgetExceeded : public std::bad_alloc {};

/**
 * @brief PMR forwarding resource enforcing charged live bytes plus explicit retained-source
 * charges. Configured exhaustion throws BudgetExceeded; upstream allocation failure propagates
 * separately.
 * @ingroup implementation
 */
class BudgetResource final : public std::pmr::memory_resource {
public:
  /**
   * @brief Initialize a PMR forwarding resource enforcing charged live bytes plus explicit
   * retained-source charges
   * @param upstream Upstream PMR resource that must outlive the owning class; null selects the
   *   default resource.
   * @param limit Maximum accounted byte count for this resource or buffer.
   */
  BudgetResource(std::pmr::memory_resource* upstream, std::size_t limit) noexcept
      : upstream(upstream ? upstream : std::pmr::get_default_resource()), limit(limit) {
  }

  /**
   * @brief Account for retained external storage without allocating through this resource.
   * @param size Requested byte count or checked input byte length.
   */
  void charge(std::size_t size) {
    if (size > limit - used)
      throw BudgetExceeded();
    used += size;
  }

  /**
   * @brief Remove the read-time allocation ceiling while retaining accounting for owned containers.
   */
  void disarm() noexcept {
    limit = std::numeric_limits<std::size_t>::max();
  }

private:
  /**
   * @brief Check the live-byte budget, allocate upstream, then charge the successful allocation.
   * @param size Requested byte count or checked input byte length.
   * @param alignment Required allocation alignment in bytes.
   * @return Aligned upstream storage; BudgetExceeded or upstream allocation exceptions propagate.
   */
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    if (size > limit - used)
      throw BudgetExceeded();
    void* memory = upstream->allocate(size, alignment);
    used += size;
    return memory;
  }

  /**
   * @brief Return storage to its original upstream resource and release its charged byte count.
   * @param memory Previously allocated storage to release through its original upstream resource.
   * @param size Requested byte count or checked input byte length.
   * @param alignment Required allocation alignment in bytes.
   */
  void do_deallocate(void* memory, std::size_t size, std::size_t alignment) override {
    upstream->deallocate(memory, size, alignment);
    used -= size;
  }

  /**
   * @brief Use resource identity rather than upstream equality for allocator interchangeability.
   * @param other Object to compare with or transfer ownership from.
   * @return True only when comparing this resource to itself.
   */
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::pmr::memory_resource* upstream; ///< Borrowed allocator forwarding all physical allocation/deallocation operations.
  std::size_t limit; ///< Maximum accounted bytes permitted by this resource or buffer.
  std::size_t used = 0; ///< Currently charged live allocation and retained-source bytes.
};

} // namespace cafe::detail
