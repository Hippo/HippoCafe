#pragma once

#include <cstdint>
#include <span>

#include "cafe/ids.hpp"

namespace cafe {

// A non-owning view/edit descriptor. MethodCode copies the targets on insert.
struct TableSwitch {
  std::int32_t low;
  LabelId default_target;
  std::span<const LabelId> targets;
};

struct LookupPair {
  std::int32_t key;
  LabelId target;

  friend bool operator==(const LookupPair&, const LookupPair&) = default;
};

// Pairs are ordered by strictly ascending signed key.
struct LookupSwitch {
  LabelId default_target;
  std::span<const LookupPair> pairs;
};

} // namespace cafe
